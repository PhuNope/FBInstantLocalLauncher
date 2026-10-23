#include "MainWindow.h"
#include "UploadDialog.h"
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidget>
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QTimer>
#include <QSslSocket>
#include <QSslError>
#include <QTcpServer>
#include <QHostAddress>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QInputDialog>
#include <QSettings>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QVersionNumber>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QPointer>
#include <QVector>
#include <memory>

static const char* kStyles = R"(
QMainWindow,QDialog { background:#151922; color:#e5eaf2; }
QLabel { color:#cbd5e1; }
QLineEdit,QSpinBox,QTableWidget { background:#202634; color:#e8eef8; border:1px solid #364153; border-radius:6px; padding:6px; }
QTableWidget { gridline-color:#303b4c; selection-background-color:#234f73; }
QHeaderView::section { background:#283243; color:#dbeafe; padding:8px; border:0; }
QPushButton { background:#306de0; color:white; padding:9px 15px; border:0; border-radius:7px; font-weight:600; }
QPushButton:hover { background:#4a85f0; }
QPushButton:disabled { background:#465165; }
QCheckBox { color:#cbd5e1; }
)";

QString MainWindow::dataFile() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath("games.json");
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), server_(new QProcess(this)) {
    setWindowTitle("FB Instant Games · Local Launcher");
    resize(1000, 580);
    setStyleSheet(kStyles);
    auto* root = new QWidget(this);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(20,20,20,20);
    layout->setSpacing(13);
    auto* heading = new QLabel("Facebook Instant Games — Local Launcher");
    heading->setStyleSheet("font-size:22px;font-weight:700;color:white;");
    layout->addWidget(heading);
    layout->addWidget(new QLabel("Manage Cocos build folders and open the Facebook Embedded Player."));
    auto* controls = new QHBoxLayout;
    auto makeButton = [&](const QString& text, auto callback) {
        auto* b = new QPushButton(text);
        controls->addWidget(b);
        connect(b, &QPushButton::clicked, this, callback);
        return b;
    };
    makeButton("+ Add game", [this]{ addGame(); });
    makeButton("Edit", [this]{ editGame(); });
    makeButton("Delete", [this]{ removeGame(); });
    makeButton("▶ Play", [this]{ playGame(); });
    makeButton("Upload FB", [this]{
        const int i = selectedIndex();
        if (i < 0) { QMessageBox::information(this, "Select a game", "Select a game before uploading."); return; }
        const QJsonObject g = games_.at(i).toObject();
        UploadDialog dialog(g.value("name").toString(), g.value("id").toString(), g.value("path").toString(), this);
        dialog.exec();
    });
    stopButton_ = makeButton("■ Stop server", [this]{ stopServer(); });
    makeButton("Open data folder", [this]{ openDataFile(); });
    makeButton("Dependencies", [this]{ showDependencies(); });
    makeButton("Updates settings", [this]{ configureUpdates(); });
    makeButton("Check updates", [this]{ checkForUpdates(true); });
    controls->addStretch();
    layout->addLayout(controls);
    table_ = new QTableWidget;
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({"Game name", "Facebook App ID", "Build directory", "Port", "HTTPS cert"});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column == 2 && row >= 0 && row < games_.size()) {
            const QString path = games_.at(row).toObject().value("path").toString();
            if (QDir(path).exists()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            else QMessageBox::warning(this, "Folder missing", "Game directory not found: " + path);
        } else {
            playGame();
        }
    });
    layout->addWidget(table_, 1);
    status_ = new QLabel("Ready");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    setCentralWidget(root);
    connect(server_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e){
        if (e == QProcess::FailedToStart)
            status_->setText("Unable to start http-server. Install Node.js + npm, then run: npm install -g http-server");
    });
    connect(server_, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus){
        serverReadyTimer_->stop(); pendingUrl_ = QUrl();
        serverDiagnostics_ += QString::fromLocal8Bit(server_->readAllStandardError());
        activePath_.clear(); activePort_ = -1;
        status_->setText(QString("Local HTTPS server stopped (exit code %1). %2").arg(code).arg(serverDiagnostics_.right(900)));
        stopButton_->setEnabled(false);
    });
    connect(server_, &QProcess::readyReadStandardOutput, this, [this] { serverDiagnostics_ += QString::fromLocal8Bit(server_->readAllStandardOutput()); });
    connect(server_, &QProcess::readyReadStandardError, this, [this] {
        const QString log = QString::fromLocal8Bit(server_->readAllStandardError());
        serverDiagnostics_ += log;
        if (log.contains("EADDRINUSE", Qt::CaseInsensitive))
            status_->setText("Port already in use. Change the game's port or stop the other server.");
    });
    serverReadyTimer_ = new QTimer(this);
    serverReadyTimer_->setInterval(300);
    connect(serverReadyTimer_, &QTimer::timeout, this, [this] {
        if (pendingUrl_.isEmpty()) { serverReadyTimer_->stop(); return; }
        if (server_->state() == QProcess::NotRunning || ++readinessAttempts_ > 40) {
            serverReadyTimer_->stop();
            pendingUrl_ = QUrl();
            status_->setText("HTTPS startup failed or timed out. " + serverDiagnostics_.right(900));
            return;
        }
        auto* probe = new QSslSocket(this);
        connect(probe, &QSslSocket::sslErrors, probe, [probe](const QList<QSslError>&) { probe->ignoreSslErrors(); });
        connect(probe, &QSslSocket::encrypted, this, [this, probe] {
            probe->deleteLater();
            if (pendingUrl_.isEmpty()) return;
            serverReadyTimer_->stop();
            QUrl url = pendingUrl_;
            pendingUrl_ = QUrl();
            status_->setText("HTTPS ready. Opening Facebook Player.");
            QDesktopServices::openUrl(url);
        });
        connect(probe, &QSslSocket::errorOccurred, probe, [probe](QAbstractSocket::SocketError) { probe->deleteLater(); });
        probe->connectToHostEncrypted("127.0.0.1", static_cast<quint16>(activePort_));
        QTimer::singleShot(250, probe, [probe] { probe->abort(); probe->deleteLater(); });
    });
    load(); refresh();
    QTimer::singleShot(650, this, [this]{ checkForUpdates(false); });
}

MainWindow::~MainWindow() { stopServer(); }

void MainWindow::load() {
    QFile file(dataFile());
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly)) { QMessageBox::warning(this,"Read error",file.errorString()); return; }
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        QMessageBox::warning(this,"Invalid saved data", "games.json is not a valid JSON array. No changes will be made until you save.");
        return;
    }
    games_ = doc.array();
}

void MainWindow::save() {
    const QString fileName = dataFile();
    if (!QDir().mkpath(QFileInfo(fileName).absolutePath())) {
        QMessageBox::warning(this,"Save error","Cannot create settings directory."); return;
    }
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(games_).toJson(QJsonDocument::Indented)) < 0 || !file.commit())
        QMessageBox::warning(this,"Save error", "Could not save " + fileName + ": " + file.errorString());
}

void MainWindow::refresh() {
    table_->setRowCount(games_.size());
    for(int i=0; i<games_.size(); ++i) {
        const QJsonObject g = games_.at(i).toObject();
        const QString cert = QDir(g.value("path").toString()).filePath("cert.pem");
        const QString sharedCert = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("certificates/cert.pem");
        const QString sharedKey = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("certificates/key.pem");
        const bool gamePair = QFileInfo::exists(cert) && QFileInfo::exists(QDir(g.value("path").toString()).filePath("key.pem"));
        const bool sharedPair = QFileInfo::exists(sharedCert) && QFileInfo::exists(sharedKey);
        const QString certStatus = gamePair ? "Game" : (QFileInfo::exists(cert) ? "Incomplete" : (sharedPair ? "Shared" : "Not configured"));
        const QStringList cells = {g.value("name").toString(),g.value("id").toString(),g.value("path").toString(),QString::number(g.value("port").toInt(8080)),certStatus};
        for (int col=0;col<cells.size();++col) table_->setItem(i,col,new QTableWidgetItem(cells.at(col)));
    }
    stopButton_->setEnabled(server_->state() != QProcess::NotRunning);
}

int MainWindow::selectedIndex() const { int row = table_->currentRow(); return row >= 0 && row < games_.size() ? row : -1; }

bool MainWindow::editDialog(QJsonObject& obj, const QString& title) {
    QDialog dialog(this); dialog.setWindowTitle(title); dialog.resize(640,260); dialog.setStyleSheet(kStyles);
    auto* form = new QFormLayout(&dialog);
    QLineEdit name(obj.value("name").toString());
    QLineEdit id(obj.value("id").toString());
    QLineEdit path(obj.value("path").toString());
    auto* browse = new QPushButton("Browse...");
    auto* pathRow = new QWidget; auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0,0,0,0); pathLayout->addWidget(&path); pathLayout->addWidget(browse);
    connect(browse,&QPushButton::clicked,&dialog,[&]{
        auto p=QFileDialog::getExistingDirectory(&dialog,"Select built game folder (contains index.html)",path.text());
        if (!p.isEmpty()) path.setText(p);
    });
    QSpinBox port; port.setRange(1024,65535); port.setValue(obj.value("port").toInt(8080));
    form->addRow("Game name", &name);
    form->addRow("Facebook App ID", &id);
    form->addRow("Cocos build folder",pathRow);
    form->addRow("Local HTTPS port",&port);
    form->addRow(new QLabel("Folder must contain index.html, cert.pem and key.pem. Requires global npm http-server."));
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(&buttons);
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        if(name.text().trimmed().isEmpty() || !QRegularExpression("^[0-9]+$").match(id.text().trimmed()).hasMatch() || !QFileInfo(QDir(path.text()).filePath("index.html")).isFile()) {
            QMessageBox::warning(&dialog,"Invalid game","Enter a name, numeric Facebook App ID and a build folder containing index.html."); return;
        }
        dialog.accept();
    });
    if(dialog.exec()!=QDialog::Accepted) return false;
    obj.insert("name",name.text().trimmed()); obj.insert("id",id.text().trimmed());
    obj.insert("path",QDir(path.text().trimmed()).absolutePath()); obj.insert("port",port.value());
    return true;
}

void MainWindow::addGame() { QJsonObject o; if(!editDialog(o,"Add game"))return; games_.append(o); save(); refresh(); table_->selectRow(games_.size()-1); }
void MainWindow::editGame() { int i=selectedIndex(); if(i<0)return; QJsonObject o=games_.at(i).toObject(); if(!editDialog(o,"Edit game"))return; games_.replace(i,o); save(); refresh(); table_->selectRow(i); }
void MainWindow::removeGame() {
    int i=selectedIndex(); if(i<0)return;
    if(QMessageBox::question(this,"Delete game","Remove this game from the launcher? Local game files will NOT be deleted.")!=QMessageBox::Yes)return;
    games_.removeAt(i); save(); refresh();
}

void MainWindow::stopServer() {
    serverReadyTimer_->stop(); pendingUrl_ = QUrl();
    if(server_->state()==QProcess::NotRunning) return;
    server_->terminate();
    if(!server_->waitForFinished(1500)) { server_->kill(); server_->waitForFinished(1500); }
    activePath_.clear(); activePort_=-1; activeCertPath_.clear(); activeKeyPath_.clear();
    stopButton_->setEnabled(false);
}

void MainWindow::playGame() {
    int i=selectedIndex(); if(i<0){ QMessageBox::information(this,"Select a game","Select a row first."); return; }
    const QJsonObject g=games_.at(i).toObject();
    const QString path=g.value("path").toString();
    const int port=g.value("port").toInt(8080);
    const QString id=g.value("id").toString();
    const QDir dir(path);
    if (!QFileInfo(dir.filePath("index.html")).isFile()) {
        QMessageBox::warning(this, "Missing game files", "Game folder must contain index.html.");
        return;
    }
    QString certPath = dir.filePath("cert.pem");
    QString keyPath = dir.filePath("key.pem");
    const bool gameCert = QFileInfo::exists(certPath);
    const bool gameKey = QFileInfo::exists(keyPath);
    if (gameCert != gameKey) {
        QMessageBox::warning(this, "Incomplete certificate",
            "Game folder has only one of cert.pem and key.pem. Add the matching file or remove the incomplete pair to use the shared certificate.");
        return;
    }
    if (!gameCert) {
        const QString sharedDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("certificates");
        QDir().mkpath(sharedDir);
        certPath = QDir(sharedDir).filePath("cert.pem");
        keyPath = QDir(sharedDir).filePath("key.pem");
        if (!QFileInfo::exists(certPath) || !QFileInfo::exists(keyPath)) {
#ifdef Q_OS_WIN
            if (QMessageBox::question(this, "Create localhost certificate",
                "No game-specific or shared TLS certificate found. Generate a shared localhost certificate using mkcert?\\n"
                "This requires mkcert installed and its local CA trusted via 'mkcert -install'.") != QMessageBox::Yes) return;
            QProcess generate;
            generate.setProgram("cmd.exe");
            generate.setArguments({"/D", "/S", "/C", "mkcert -cert-file \"" + certPath + "\" -key-file \"" + keyPath + "\" localhost 127.0.0.1 ::1"});
            generate.start();
            if (!generate.waitForStarted(3000) || !generate.waitForFinished(20000) || generate.exitCode() != 0 ||
                !QFileInfo::exists(certPath) || !QFileInfo::exists(keyPath)) {
                QMessageBox::warning(this, "Certificate creation failed",
                    "Install mkcert in Dependencies, run 'mkcert -install' once, and retry.\\n" +
                    QString::fromLocal8Bit(generate.readAllStandardError()));
                return;
            }
#else
            QMessageBox::warning(this, "Missing certificate", "Generate a shared certificate with mkcert.");
            return;
#endif
        }
    }
    if (server_->state() != QProcess::NotRunning && (activePath_ != path || activePort_ != port || activeCertPath_ != certPath || activeKeyPath_ != keyPath)) stopServer();
    if (server_->state() == QProcess::NotRunning) {
        QTcpServer portProbe;
        if (!portProbe.listen(QHostAddress::LocalHost, static_cast<quint16>(port))) {
            QMessageBox::warning(this, "HTTPS port already in use",
                QString("127.0.0.1:%1 is already occupied. Another server (possibly one left from a previous Launcher session) is running.\n\n"
                        "Close that server or choose another port in Edit Game.\n\n"
                        "To identify the process in PowerShell:\n"
                        "Get-NetTCPConnection -LocalPort %1 -State Listen | Select-Object OwningProcess\n"
                        "Then: Get-Process -Id <PID>\n\n"
                        "Launcher will not stop an unknown process automatically.").arg(port));
            status_->setText(QString("Port %1 is occupied; HTTPS server was not started.").arg(port));
            return;
        }
        portProbe.close();
    }
    if (server_->state() == QProcess::NotRunning) {
        serverDiagnostics_.clear();
#ifdef Q_OS_WIN
        // Launch Node directly: cmd.exe /C http-server leaves node.exe orphaned
        // when the parent shell is terminated on Windows.
        const QString nodeExe = QStandardPaths::findExecutable("node.exe");
        QProcess npm;
        npm.start("cmd.exe", {"/D", "/C", "npm root -g"});
        if (nodeExe.isEmpty() || !npm.waitForStarted(3000) || !npm.waitForFinished(5000) || npm.exitCode() != 0) {
            QMessageBox::warning(this, "Missing Node.js", "Install Node.js and http-server using Dependencies, then restart Launcher.");
            return;
        }
        const QString globalModules = QString::fromLocal8Bit(npm.readAllStandardOutput()).trimmed().split('\n').last().trimmed();
        const QString serverEntry = QDir(globalModules).filePath("http-server/bin/http-server");
        if (!QFileInfo(serverEntry).isFile()) {
            QMessageBox::warning(this, "Missing http-server", "Cannot find global http-server package. Run: npm install -g http-server");
            return;
        }
        server_->setProgram(nodeExe);
        server_->setArguments({serverEntry, "-S", "-C", certPath, "-K", keyPath, "-p", QString::number(port), "-a", "127.0.0.1", "-c-1", "."});
#else
        server_->setProgram("http-server");
        server_->setArguments({"-S", "-C", certPath, "-K", keyPath, "-p", QString::number(port), "-a", "127.0.0.1", "-c-1", "."});
#endif
        server_->setWorkingDirectory(path);
        server_->start();
        if(!server_->waitForStarted(4000)) {
            QMessageBox::critical(this,"Could not start server", "Check that Node.js and npm http-server are installed and available on PATH.\n" + server_->errorString());return;
        }
        activePath_=path; activePort_=port; activeCertPath_=certPath; activeKeyPath_=keyPath;
    }
    stopButton_->setEnabled(true);
    QUrl url(QString("https://www.facebook.com/embed/instantgames/%1/player").arg(id));
    QUrlQuery query; query.addQueryItem("game_url",QString("https://localhost:%1").arg(port)); url.setQuery(query);
    pendingUrl_ = url;
    readinessAttempts_ = 0;
    status_->setText("Waiting for HTTPS server to become ready...");
    serverReadyTimer_->start();
}

namespace {
struct Dependency {
    const char* name;
    const char* executable;
    QStringList checkArgs;
    const char* installCommand;
    const char* purpose;
};
QVector<Dependency> dependencyList() {
    return {
        {"Node.js", "node", {"--version"}, "winget install --id OpenJS.NodeJS.LTS -e --accept-package-agreements --accept-source-agreements", "Required runtime"},
        {"npm", "npm", {"--version"}, "winget install --id OpenJS.NodeJS.LTS -e --accept-package-agreements --accept-source-agreements", "Node package manager"},
        {"http-server", "http-server", {"--version"}, "npm install -g http-server", "Required HTTPS server"},
        {"mkcert", "mkcert", {"-version"}, "winget install --id FiloSottile.mkcert -e --accept-package-agreements --accept-source-agreements", "Optional: trusted SSL certificates"}
    };
}
bool dependencyAvailable(const Dependency& dep, QString& version) {
    QProcess process;
#ifdef Q_OS_WIN
    process.setProgram("cmd.exe");
    QString command = QString::fromLatin1(dep.executable);
    for (const auto& arg : dep.checkArgs) command += " " + arg;
    process.setArguments({"/D", "/S", "/C", command});
#else
    process.setProgram(QString::fromLatin1(dep.executable));
    process.setArguments(dep.checkArgs);
#endif
    process.start();
    if (!process.waitForStarted(2000)) return false;
    if (!process.waitForFinished(4000)) { process.kill(); process.waitForFinished(500); return false; }
    version = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    if (version.isEmpty()) version = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
}

void MainWindow::showDependencies() {
    QDialog dialog(this);
    dialog.setWindowTitle("Dependencies");
    dialog.resize(760, 390);
    dialog.setStyleSheet(kStyles);
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel("Required: Node.js, npm, http-server. Optional: mkcert for browser-trusted localhost certificates.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({"Library", "Status", "Purpose", "Action"});
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    auto deps = dependencyList();
    table->setRowCount(deps.size());
    const auto refresh = [table, deps]() {
        for (int i = 0; i < deps.size(); ++i) {
            QString version;
            bool installed = dependencyAvailable(deps.at(i), version);
            table->setItem(i, 0, new QTableWidgetItem(QString::fromLatin1(deps.at(i).name)));
            table->setItem(i, 1, new QTableWidgetItem(installed ? "Installed: " + version.left(40) : "Missing / PATH"));
            table->setItem(i, 2, new QTableWidgetItem(QString::fromLatin1(deps.at(i).purpose)));
            auto* button = new QPushButton(installed ? "Reinstall" : "Install");
            table->setCellWidget(i, 3, button);
            QObject::connect(button, &QPushButton::clicked, table, [dep=deps.at(i), table]() {
                const QString command = QString::fromLatin1(dep.installCommand);
                if (QMessageBox::question(table, "Install dependency", "Run the following command in PowerShell?\n\n" + command) != QMessageBox::Yes) return;
#ifdef Q_OS_WIN
                if (!QProcess::startDetached("powershell.exe", {"-NoLogo", "-NoProfile", "-NoExit", "-Command", command}))
                    QMessageBox::warning(table, "Failed to start", "Open PowerShell and run:\n" + command);
#else
                QMessageBox::information(table, "Windows only", "Quick install requires Windows.\n" + command);
#endif
            });
        }
        table->resizeRowsToContents();
    };
    layout->addWidget(table);
    auto* actions = new QHBoxLayout;
    auto* refreshButton = new QPushButton("Refresh checks");
    auto* closeButton = new QPushButton("Close");
    actions->addWidget(refreshButton);
    actions->addStretch();
    actions->addWidget(closeButton);
    layout->addLayout(actions);
    connect(refreshButton, &QPushButton::clicked, &dialog, refresh);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    refresh();
    dialog.exec();
}

void MainWindow::openDataFile() {
    QDir().mkpath(QFileInfo(dataFile()).absolutePath());
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(dataFile()).absolutePath()));
}


#ifndef LAUNCHER_VERSION
#define LAUNCHER_VERSION "1.1.0"
#endif
#ifndef LAUNCHER_GITHUB_REPO
#define LAUNCHER_GITHUB_REPO ""
#endif

QString MainWindow::updateRepository() const {
    QSettings settings("CocosTools", "FBInstantLocalLauncher");
    return settings.value("githubRepository", QString::fromUtf8(LAUNCHER_GITHUB_REPO)).toString().trimmed();
}

void MainWindow::configureUpdates() {
    bool ok = false;
    const QString repo = QInputDialog::getText(this, "GitHub updates",
        "Public GitHub repository (owner/name):", QLineEdit::Normal,
        updateRepository(), &ok).trimmed();
    if (!ok) return;
    if (!QRegularExpression("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$").match(repo).hasMatch()) {
        QMessageBox::warning(this, "Invalid repository", "Enter owner/repository, for example my-org/my-launcher.");
        return;
    }
    QSettings("CocosTools", "FBInstantLocalLauncher").setValue("githubRepository", repo);
    checkForUpdates(true);
}

void MainWindow::checkForUpdates(bool manual) {
    const QString repo = updateRepository();
    if (!QRegularExpression("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$").match(repo).hasMatch()) {
        if (manual) configureUpdates();
        return;
    }
    const QUrl api("https://api.github.com/repos/" + repo + "/releases/latest");
    QNetworkRequest req(api);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "FBInstantLocalLauncher-Updater");
    req.setTransferTimeout(15000);
    auto* reply = network_.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual, repo] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            if (manual) QMessageBox::warning(this, "Update check failed",
                QString("Cannot query GitHub releases for %1. HTTP %2: %3\nPrivate repositories require authentication.")
                  .arg(repo).arg(status).arg(reply->errorString()));
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) { if(manual) QMessageBox::warning(this, "Update check", "Invalid GitHub response."); return; }
        const auto release = doc.object();
        const QString tag = release.value("tag_name").toString();
        const QString newVersion = tag.startsWith('v') ? tag.mid(1) : tag;
        const QVersionNumber current = QVersionNumber::fromString(QString::fromUtf8(LAUNCHER_VERSION));
        const QVersionNumber latest = QVersionNumber::fromString(newVersion);
        if (latest.isNull() || QVersionNumber::compare(latest, current) <= 0) {
            if(manual) QMessageBox::information(this, "Updates", "Already up to date (v" + QString::fromUtf8(LAUNCHER_VERSION) + ").");
            return;
        }
        QUrl assetUrl;
        for (const auto& a : release.value("assets").toArray()) {
            const auto asset = a.toObject();
            const QString name = asset.value("name").toString();
            if (name == QString("FBInstantLocalLauncher-%1-windows-x64.zip").arg(tag)) {
                assetUrl = QUrl(asset.value("browser_download_url").toString()); break;
            }
        }
        if (!assetUrl.isValid() || assetUrl.scheme() != "https") {
            if(manual) QMessageBox::warning(this, "Update unavailable", "New release found, but its Windows x64 ZIP is missing.");
            return;
        }
        const auto answer = QMessageBox::question(this, "Update available",
            QString("Current: v%1\nLatest: %2\n\nDownload and install update now? The app will restart.")
                .arg(QString::fromUtf8(LAUNCHER_VERSION), tag),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes) downloadUpdate(tag, assetUrl);
    });
}

void MainWindow::downloadUpdate(const QString& tag, const QUrl& assetUrl) {
#ifndef Q_OS_WIN
    QMessageBox::information(this, "Windows only", "Automatic installation is supported on Windows only.");
    return;
#else
    const QString script = QDir(QCoreApplication::applicationDirPath()).filePath("updater.ps1");
    if (!QFileInfo::exists(script)) {
        QMessageBox::critical(this, "Updater missing", "updater.ps1 was not deployed beside the executable."); return;
    }
    if (assetUrl.host() != "github.com" && assetUrl.host() != "objects.githubusercontent.com") {
        QMessageBox::critical(this, "Unexpected URL", "Release asset URL is not hosted on GitHub."); return;
    }
    auto* progress = new QProgressDialog("Downloading " + tag + "...", "Cancel", 0, 100, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    auto* reply = network_.get(QNetworkRequest(assetUrl));
    reply->setReadBufferSize(16 * 1024 * 1024);
    auto* bytes = new QByteArray();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes] { bytes->append(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, progress, [progress](qint64 got, qint64 total) {
        if (total > 0) progress->setValue(static_cast<int>(got * 100 / total));
    });
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, progress, bytes, script, tag] {
        bytes->append(reply->readAll());
        const auto error = reply->error();
        const QString errorString = reply->errorString();
        reply->deleteLater();
        if (progress) { progress->close(); progress->deleteLater(); }
        std::unique_ptr<QByteArray> owned(bytes);
        if (error != QNetworkReply::NoError) {
            if (error != QNetworkReply::OperationCanceledError)
                QMessageBox::critical(this, "Download failed", errorString);
            return;
        }
        const QString destination = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath("FBInstantLocalLauncher-update-" + tag + ".zip");
        QSaveFile zip(destination);
        if (!zip.open(QIODevice::WriteOnly) || zip.write(*bytes) != bytes->size() || !zip.commit()) {
            QMessageBox::critical(this, "Download failed", "Cannot save update ZIP: " + zip.errorString()); return;
        }
        QStringList args = {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", script,
            "-ZipPath", destination, "-TargetDir", QCoreApplication::applicationDirPath(),
            "-ExeName", QFileInfo(QCoreApplication::applicationFilePath()).fileName(),
            "-WaitPid", QString::number(QCoreApplication::applicationPid())};
        if (!QProcess::startDetached("powershell.exe", args, QCoreApplication::applicationDirPath())) {
            QMessageBox::critical(this, "Update failed", "Cannot start updater PowerShell process."); return;
        }
        QCoreApplication::quit();
    });
#endif
}
