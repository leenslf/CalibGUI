#include "SshProcess.h"

#include <QProcessEnvironment>

#include "config.h"

void ConfigureSshpassProcess(QProcess *process, const std::string &sshTarget,
                              const std::string &sshPassword,
                              const QStringList &remoteCommand,
                              bool allocatePty) {
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert("SSHPASS", QString::fromStdString(sshPassword));
  process->setProcessEnvironment(env);

  QStringList args { "-e", "ssh" };
  if (allocatePty) args << "-tt";
  args << "-o" << "BatchMode=no"
       << "-o" << QString("ConnectTimeout=%1").arg(SSH_CONNECT_TIMEOUT_SECS)
       << "-o" << "StrictHostKeyChecking=accept-new"
       << QString::fromStdString(sshTarget)
       << remoteCommand;

  process->setProgram("sshpass");
  process->setArguments(args);
}
