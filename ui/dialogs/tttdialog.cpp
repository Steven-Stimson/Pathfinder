#include "tttdialog.h"
#include "ui_tttdialog.h"

#include "graph/gfawriter.h"
#include "graph/assemblygraph.h"
#include "graph/debruijnnode.h"
#include "graph/graphicsitemnode.h"
#include "ui/bandagegraphicsview.h"
#include "ui/bandagegraphicsscene.h"
#include "program/globals.h"
#include "program/settings.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QUuid>
#include <QTextStream>
#include <QFile>
#include <QStandardPaths>
#include <QScrollBar>
#include <QDateTime>
#include <QFileInfo>
#include <QCoreApplication>
#include <QTimer>

TTTDialog::TTTDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::TTTDialog)
{
    ui->setupUi(this);

    loadSettings();

    // Auto-detect the embedded ttt binary
    if (m_tttBinaryPath.isEmpty()) {
        QStringList searchPaths = {
            QCoreApplication::applicationDirPath() + "/../thirdparty/TTT/ttt",
            QCoreApplication::applicationDirPath() + "/../../thirdparty/TTT/ttt",
#ifdef Q_OS_WIN
            QCoreApplication::applicationDirPath() + "/../thirdparty/TTT/ttt.exe",
            QCoreApplication::applicationDirPath() + "/../../thirdparty/TTT/ttt.exe",
#endif
        };
        for (const auto &p : searchPaths) {
            QString absPath = QFileInfo(p).absoluteFilePath();
            if (QFile::exists(absPath)) {
                m_tttBinaryPath = absPath;
                break;
            }
        }
    }

    connect(ui->browseGafButton, &QPushButton::clicked, this, &TTTDialog::browseGaf);
    connect(ui->browseCoverageButton, &QPushButton::clicked, this, &TTTDialog::browseCoverage);
    connect(ui->browseBoundaryButton, &QPushButton::clicked, this, &TTTDialog::browseBoundaryNodesFile);
    connect(ui->selectBoundaryButton, &QPushButton::clicked, this, &TTTDialog::startBoundarySelection);
    connect(ui->clearBoundaryButton, &QPushButton::clicked, this, &TTTDialog::clearBoundaryPairs);
    connect(ui->browseOutputButton, &QPushButton::clicked, this, &TTTDialog::browseOutputDir);

    connect(ui->runButton, &QPushButton::clicked, this, &TTTDialog::runTTT);
    connect(ui->cancelButton, &QPushButton::clicked, this, &TTTDialog::cancelTTT);

    setMinimumSize(600, 500);
}

TTTDialog::~TTTDialog()
{
    saveSettings();
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
    delete ui;
}

void TTTDialog::loadSettings()
{
    ui->gafPathEdit->setText(g_settings->tttGafPath);
    ui->coveragePathEdit->setText(g_settings->tttCoveragePath);
    ui->qualityThresholdSpinBox->setValue(g_settings->tttQualityThreshold > 0 ? g_settings->tttQualityThreshold : 20);
    ui->mipTimeLimitSpinBox->setValue(g_settings->tttMipTimeLimit > 0 ? g_settings->tttMipTimeLimit : 7200);
    ui->initialPathsSpinBox->setValue(g_settings->tttNumInitialPaths > 0 ? g_settings->tttNumInitialPaths : 10);
    ui->maxIterSpinBox->setValue(g_settings->tttMaxIterations > 0 ? g_settings->tttMaxIterations : 100000);
    ui->outputModeComboBox->setCurrentIndex(g_settings->tttOutputMode);
}

void TTTDialog::saveSettings()
{
    g_settings->tttGafPath = ui->gafPathEdit->text();
    g_settings->tttCoveragePath = ui->coveragePathEdit->text();
    g_settings->tttQualityThreshold = ui->qualityThresholdSpinBox->value();
    g_settings->tttMipTimeLimit = ui->mipTimeLimitSpinBox->value();
    g_settings->tttNumInitialPaths = ui->initialPathsSpinBox->value();
    g_settings->tttMaxIterations = ui->maxIterSpinBox->value();
    g_settings->tttOutputMode = ui->outputModeComboBox->currentIndex();
}

void TTTDialog::browseGaf()
{
    QString path = QFileDialog::getOpenFileName(this, "Select GAF Alignment File",
                                                 QString(), "All files (*)");
    if (!path.isEmpty())
        ui->gafPathEdit->setText(path);
}

void TTTDialog::browseCoverage()
{
    QString path = QFileDialog::getOpenFileName(this, "Select Coverage File",
                                                 QString(), "All files (*)");
    if (!path.isEmpty())
        ui->coveragePathEdit->setText(path);
}

void TTTDialog::browseBoundaryNodesFile()
{
    QString path = QFileDialog::getOpenFileName(this, "Select Boundary Nodes TSV File",
                                                 QString(), "All files (*)");
    if (!path.isEmpty()) {
        m_boundaryTsvPath = path;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            ui->boundaryPairsEdit->setPlainText(QString::fromUtf8(file.readAll()));
            file.close();
        }
    }
}

void TTTDialog::startBoundarySelection()
{
    if (!g_graphicsView || !g_graphicsView->scene()) {
        QMessageBox::information(this, "No Graph", "Please load a graph first.");
        return;
    }

    auto *scene = dynamic_cast<BandageGraphicsScene *>(g_graphicsView->scene());
    if (!scene) return;

    auto nodes = scene->getSelectedPositiveNodes();
    if (nodes.size() < 2) {
        QMessageBox::information(this, "Select Nodes",
            QString("Please select %1 more node(s) to complete a pair.\n\n"
                    "Hold Ctrl and click on nodes in the graph.\n"
                    "Select pairs in order: entry → exit.")
            .arg(2 - static_cast<int>(nodes.size())));
        return;
    }

    QString entry = nodes[0]->getName();
    QString exit = nodes[1]->getName();
    if (entry.endsWith('+') || entry.endsWith('-'))
        entry.chop(1);
    if (exit.endsWith('+') || exit.endsWith('-'))
        exit.chop(1);

    QString current = ui->boundaryPairsEdit->toPlainText().trimmed();
    if (!current.isEmpty())
        current += "\n";
    current += entry + "\t" + exit;
    ui->boundaryPairsEdit->setPlainText(current);

    generateBoundaryTsv();

    scene->clearSelection();
    g_graphicsView->viewport()->update();

    appendLog(QString("[Added boundary pair: %1 → %2]\n").arg(entry, exit));
}

void TTTDialog::clearBoundaryPairs()
{
    ui->boundaryPairsEdit->clear();
    m_boundaryTsvPath.clear();
}

bool TTTDialog::generateBoundaryTsv()
{
    QString text = ui->boundaryPairsEdit->toPlainText().trimmed();
    if (text.isEmpty())
        return false;

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString tsvDir;
    if (!ui->outputDirEdit->text().isEmpty()) {
        tsvDir = ui->outputDirEdit->text();
    } else {
        tsvDir = QDir::temp().filePath("pathfinder_ttt_boundary");
    }
    QDir().mkpath(tsvDir);
    m_boundaryTsvPath = tsvDir + "/boundary_nodes_" + timestamp + ".tsv";

    QFile file(m_boundaryTsvPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);
    out << text << "\n";
    file.close();
    return true;
}

void TTTDialog::browseOutputDir()
{
    QString path = QFileDialog::getExistingDirectory(this, "Select Output Directory");
    if (!path.isEmpty())
        ui->outputDirEdit->setText(path);
}

bool TTTDialog::validateInputs()
{
    if (m_tttBinaryPath.isEmpty() || !QFile::exists(m_tttBinaryPath)) {
        QMessageBox::warning(this, "Missing Binary", "TTT binary not found. Please reinstall the application.");
        return false;
    }
    return true;
}

bool TTTDialog::writeCurrentGraphGfa(const QString &path)
{
    if (!g_assemblyGraph) return false;
    return gfa::saveEntireGraph(path, *g_assemblyGraph);
}

void TTTDialog::runTTT()
{
    if (!validateInputs())
        return;

    saveSettings();

    if (ui->outputDirEdit->text().isEmpty()) {
        m_outputDir = QDir::temp().filePath("pathfinder_ttt_" +
            QUuid::createUuid().toString(QUuid::WithoutBraces));
    } else {
        m_outputDir = ui->outputDirEdit->text();
    }
    QDir().mkpath(m_outputDir);
    ui->outputDirEdit->setText(m_outputDir);

    m_inputGfaPath = m_outputDir + "/input.gfa";
    if (!writeCurrentGraphGfa(m_inputGfaPath)) {
        QMessageBox::warning(this, "Error", "Failed to export current graph to GFA.");
        return;
    }

    QStringList args;
    args << "--graph" << m_inputGfaPath
         << "--outdir" << m_outputDir
         << "--quality-threshold" << QString::number(ui->qualityThresholdSpinBox->value())
         << "--milp-time-limit" << QString::number(ui->mipTimeLimitSpinBox->value())
         << "--num-initial-paths" << QString::number(ui->initialPathsSpinBox->value())
         << "--max-iterations" << QString::number(ui->maxIterSpinBox->value());

    if (!ui->gafPathEdit->text().isEmpty() && QFile::exists(ui->gafPathEdit->text()))
        args << "--alignment" << ui->gafPathEdit->text();

    if (!ui->coveragePathEdit->text().isEmpty() && QFile::exists(ui->coveragePathEdit->text()))
        args << "--coverage" << ui->coveragePathEdit->text();

    QString boundaryPath;
    if (!m_boundaryTsvPath.isEmpty() && QFile::exists(m_boundaryTsvPath)) {
        boundaryPath = m_boundaryTsvPath;
    }
    if (!boundaryPath.isEmpty()) {
        args << "--boundary-nodes" << boundaryPath;
    }

    // Always request GFA output from the ttt binary itself
    args << "--output-gfa";
    QString modeStr;
    int mode = ui->outputModeComboBox->currentIndex();
    if (mode == 0)      modeStr = "all";
    else if (mode == 1) modeStr = "merged";
    else if (mode == 2) modeStr = "per-path";
    else                modeStr = "concatenated";
    args << "--output-mode" << modeStr;

    ui->logTextEdit->clear();
    appendLog("=== TTT Command ===\n");
    appendLog(m_tttBinaryPath + " " + args.join(" ") + "\n\n");
    appendLog("=== TTT Output ===\n");

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_outputDir);

    // Add thirdparty/TTT to PATH so the ttt binary can find glpsol
    auto env = QProcessEnvironment::systemEnvironment();
    QString tttDir = QFileInfo(m_tttBinaryPath).absolutePath();
    QString currentPath = env.value("PATH");
#ifdef Q_OS_WIN
    env.insert("PATH", tttDir + ";" + currentPath);
#else
    env.insert("PATH", tttDir + ":" + currentPath);
#endif
    m_process->setProcessEnvironment(env);

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TTTDialog::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &TTTDialog::onProcessError);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &TTTDialog::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &TTTDialog::onReadyReadStderr);

    m_process->start(m_tttBinaryPath, args);
    setRunningState(true);
}

void TTTDialog::cancelTTT()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        appendLog("\n[TTT process cancelled by user]\n");
        setRunningState(false);
        return;
    }
    reject();
}

void TTTDialog::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    setRunningState(false);

    if (exitStatus == QProcess::CrashExit) {
        appendLog("\n[TTT process crashed]\n");
        return;
    }

    appendLog(QString("\n[TTT process exited with code %1]\n").arg(exitCode));

    if (exitCode == 0) {
        QDir outDir(m_outputDir);

        // Load the appropriate GFA based on output mode
        int mode = ui->outputModeComboBox->currentIndex();
        if (mode == 0 || mode == 3) {
            // All or Concatenated: prefer concatenated
            QString catGfa = m_outputDir + "/traversal_concatenated.gfa";
            if (QFile::exists(catGfa)) {
                m_outputGfaPath = catGfa;
            }
        } else if (mode == 1) {
            // Merged
            QString mergedGfa = m_outputDir + "/traversal.gfa";
            if (QFile::exists(mergedGfa))
                m_outputGfaPath = mergedGfa;
        } else {
            // Per-path: load path0
            QString path0Gfa = m_outputDir + "/traversal_path0.gfa";
            if (QFile::exists(path0Gfa))
                m_outputGfaPath = path0Gfa;
        }

        if (m_outputGfaPath.isEmpty()) {
            QStringList gfaFiles = outDir.entryList({"*.gfa"}, QDir::Files);
            if (!gfaFiles.isEmpty())
                m_outputGfaPath = m_outputDir + "/" + gfaFiles.first();
        }

        if (!m_outputGfaPath.isEmpty())
            appendLog(QString("\nLoading: %1\n").arg(m_outputGfaPath));

        appendLog("\nTTT completed successfully.\n");
        accept();
    } else {
        appendLog("\nTTT failed. Check the output above for errors.\n");
    }
}

void TTTDialog::onProcessError(QProcess::ProcessError error)
{
    QString errorMsg;
    switch (error) {
    case QProcess::FailedToStart:
        errorMsg = "Failed to start TTT binary.";
        break;
    case QProcess::Timedout:
        errorMsg = "TTT process timed out.";
        break;
    default:
        errorMsg = QString("TTT process error: %1").arg(error);
        break;
    }
    appendLog("\n[ERROR] " + errorMsg + "\n");
    setRunningState(false);
}

void TTTDialog::onReadyReadStdout()
{
    if (m_process)
        appendLog(QString::fromUtf8(m_process->readAllStandardOutput()));
}

void TTTDialog::onReadyReadStderr()
{
    if (m_process)
        appendLog(QString::fromUtf8(m_process->readAllStandardError()));
}

void TTTDialog::appendLog(const QString &text)
{
    ui->logTextEdit->moveCursor(QTextCursor::End);
    ui->logTextEdit->insertPlainText(text);
    ui->logTextEdit->moveCursor(QTextCursor::End);
}

void TTTDialog::setRunningState(bool running)
{
    ui->runButton->setEnabled(!running);
    ui->gafPathEdit->setEnabled(!running);
    ui->coveragePathEdit->setEnabled(!running);
    ui->selectBoundaryButton->setEnabled(!running);
    ui->clearBoundaryButton->setEnabled(!running);
    ui->browseBoundaryButton->setEnabled(!running);
    ui->outputDirEdit->setEnabled(!running);
    ui->qualityThresholdSpinBox->setEnabled(!running);
    ui->mipTimeLimitSpinBox->setEnabled(!running);
    ui->initialPathsSpinBox->setEnabled(!running);
    ui->maxIterSpinBox->setEnabled(!running);
    ui->browseGafButton->setEnabled(!running);
    ui->browseCoverageButton->setEnabled(!running);
    ui->browseOutputButton->setEnabled(!running);
    ui->cancelButton->setText(running ? "Kill TTT" : "Cancel");
}
