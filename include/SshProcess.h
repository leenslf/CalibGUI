#ifndef SSHPROCESS_H
#define SSHPROCESS_H

#include <string>

#include <QProcess>
#include <QStringList>

/** Configures `process` to run one remote command over password-authenticated SSH,
    via `sshpass -e ssh -o ... <sshTarget> <remoteCommand...>`. Used for every
    SSH call CalibGUI makes (listing cameras, streaming, killing the remote
    stream) so the connection options and password handling live in one place. */
void ConfigureSshpassProcess(QProcess *process, const std::string &sshTarget,
                              const std::string &sshPassword,
                              const QStringList &remoteCommand,
                              bool allocatePty = false);

#endif
