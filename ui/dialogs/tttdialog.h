#pragma once

#include <QDialog>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace Ui {
class TTTDialog;
}

class TTTDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TTTDialog(QWidget *parent = nullptr);
    ~TTTDialog() override;

    QString outputGfaPath() const { return m_outputGfaPath; }

private slots:
    void browseGaf();
    void removeSelectedAlignment();
    void clearAlignment();
    void browseCoverage();
    void browseBoundaryNodesFile();
    void startBoundarySelection();
    void clearBoundaryPairs();
    void browseOutputDir();
    void runTTT();
    void cancelTTT();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onReadyReadStdout();
    void onReadyReadStderr();

private:
    Ui::TTTDialog *ui;
    QProcess *m_process = nullptr;
    QString m_outputDir;
    QString m_outputGfaPath;
    QString m_boundaryTsvPath;
    QString m_inputGfaPath;
    QString m_tttBinaryPath;
    QStringList m_alignmentFiles;

    void setRunningState(bool running);
    void appendLog(const QString &text);
    bool validateInputs();
    bool generateBoundaryTsv();
    bool writeCurrentGraphGfa(const QString &path);
    void saveSettings();
    void loadSettings();
};
