#include "UploadDialog.h"
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QDialogButtonBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QUrl>
#include <QVBoxLayout>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

namespace {
QString credentialName(const QString& appId) {
    return "FBInstantLocalLauncher/MetaUpload/" + appId;
}
bool runPowerShell(const QString& script, const QString& path, QString& error, const QString& staging = {}) {
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("FBIL_ZIP_PATH", path);
    env.insert("FBIL_STAGE_PATH", staging);
    process.setProcessEnvironment(env);
    process.start("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command", script});
    if (!process.waitForStarted(3000) || !process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(1000);
        error = "Unable to run PowerShell archive tool.";
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        error = QString::fromLocal8Bit(process.readAllStandardError()).right(1600);
        if (error.isEmpty()) error = "PowerShell archive operation failed.";
        return false;
    }
    return true;
}
}

UploadDialog::UploadDialog(const QString& gameName, const QString& appId, const QString& buildPath, QWidget* parent)
    : QDialog(parent), gameName_(gameName), appId_(appId), buildPath_(buildPath) {
    setWindowTitle("Upload Facebook Instant Games — " + gameName_);
    resize(690, 390);
    setStyleSheet("QDialog{background:#151922;color:#e5eaf2;} QLabel,QRadioButton{color:#dbeafe;} "
                  "QLineEdit{background:#202634;color:white;border:1px solid #364153;padding:7px;} "
                  "QPushButton{background:#306de0;color:white;border:0;border-radius:5px;padding:8px;}");
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("App ID: " + appId_ + " | Upload only — never publishes to Production."));
    existing_ = new QRadioButton("Upload existing ZIP (Cocos bundle)");
    pack_ = new QRadioButton("Package build directory into a new ZIP");
    existing_->setChecked(true);
    layout->addWidget(existing_);
    layout->addWidget(pack_);
    auto* zipRow = new QHBoxLayout;
    zipPath_ = new QLineEdit;
    zipPath_->setPlaceholderText("Select the bundle .zip");
    auto* browse = new QPushButton("Browse ZIP");
    zipRow->addWidget(zipPath_);
    zipRow->addWidget(browse);
    layout->addLayout(zipRow);
    connect(browse, &QPushButton::clicked, this, &UploadDialog::chooseZip);
    connect(pack_, &QRadioButton::toggled, this, [this, browse](bool checked) {
        zipPath_->setEnabled(!checked);
        browse->setEnabled(!checked);
    });
    QDir dir(buildPath_);
    const auto candidates = dir.entryList({"*.zip"}, QDir::Files);
    if (!candidates.isEmpty()) zipPath_->setText(dir.filePath(candidates.first()));
    auto* form = new QFormLayout;
    tokenInput_ = new QLineEdit;
    tokenInput_->setEchoMode(QLineEdit::Password);
    tokenInput_->setPlaceholderText("Meta Instant Games Asset Upload Access Token");
    tokenInput_->setText(readToken());
    auto* tokenRow = new QWidget(this);
    auto* tokenLayout = new QHBoxLayout(tokenRow);
    tokenLayout->setContentsMargins(0, 0, 0, 0);
    tokenLayout->addWidget(tokenInput_, 1);
    auto* toggleToken = new QPushButton("Show", tokenRow);
    toggleToken->setCheckable(true);
    toggleToken->setToolTip("Show or hide the locally saved upload token");
    tokenLayout->addWidget(toggleToken);
    connect(toggleToken, &QPushButton::toggled, this, [this, toggleToken](bool visible) {
        tokenInput_->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        toggleToken->setText(visible ? "Hide" : "Show");
    });
    auto* getToken = new QPushButton("Get App Token", tokenRow);
    tokenLayout->addWidget(getToken);
    connect(getToken, &QPushButton::clicked, this, &UploadDialog::getAppAccessToken);
    form->addRow("Upload token", tokenRow);
    commentInput_ = new QLineEdit("Uploaded via FBInstantLocalLauncher");
    form->addRow("Comment", commentInput_);
    layout->addLayout(form);
    auto* save = new QPushButton("Save token securely (Windows Credential Manager)");
    layout->addWidget(save);
    connect(save, &QPushButton::clicked, this, &UploadDialog::saveToken);
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setValue(0);
    layout->addWidget(progress_);
    status_ = new QLabel("Ready. Bundle must contain index.html and fbapp-config.json at ZIP root.");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto* buttons = new QHBoxLayout;
    uploadButton_ = new QPushButton("Upload to Web Hosting");
    auto* close = new QPushButton("Close");
    buttons->addWidget(uploadButton_);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(uploadButton_, &QPushButton::clicked, this, &UploadDialog::beginUpload);
    connect(close, &QPushButton::clicked, this, [this] {
        if (packaging_) return;
        if (currentReply_) currentReply_->abort();
        reject();
    });
}

void UploadDialog::reject() {
    if (packaging_) return;
    QDialog::reject();
}

void UploadDialog::chooseZip() {
    const QString file = QFileDialog::getOpenFileName(this, "Choose Instant Games bundle", buildPath_, "ZIP archives (*.zip)");
    if (!file.isEmpty()) zipPath_->setText(file);
}

QString UploadDialog::readToken() const {
#ifdef Q_OS_WIN
    const auto target = credentialName(appId_).toStdWString();
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) return {};
    QString token = QString::fromUtf8(reinterpret_cast<const char*>(credential->CredentialBlob),
                                       static_cast<int>(credential->CredentialBlobSize));
    CredFree(credential);
    return token;
#else
    return {};
#endif
}

bool UploadDialog::storeToken(const QString& token) {
#ifdef Q_OS_WIN
    const auto target = credentialName(appId_).toStdWString();
    QByteArray bytes = token.toUtf8();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(target.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(bytes.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"FBInstantLocalLauncher");
    return CredWriteW(&credential, 0) != 0;
#else
    Q_UNUSED(token)
    return false;
#endif
}

void UploadDialog::getAppAccessToken() {
    QDialog dialog(this);
    dialog.setWindowTitle("Get Meta App Access Token");
    dialog.setStyleSheet(styleSheet());
    auto* layout = new QVBoxLayout(&dialog);
    auto* note = new QLabel("Enter the App Secret from Meta App Settings > Basic.\\n"
        "The secret will not be saved. App token upload permission is not guaranteed.", &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* form = new QFormLayout;
    auto* id = new QLineEdit(appId_, &dialog);
    id->setReadOnly(true);
    auto* secret = new QLineEdit(&dialog);
    secret->setEchoMode(QLineEdit::Password);
    form->addRow("App ID", id);
    form->addRow("App Secret", secret);
    layout->addLayout(form);
    auto* controls = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(controls);
    connect(controls, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(controls, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    QString secretValue = secret->text().trimmed();
    secret->clear();
    if (secretValue.isEmpty()) {
        QMessageBox::warning(this, "Missing App Secret", "Enter App Secret.");
        return;
    }
    QUrlQuery body;
    body.addQueryItem("client_id", appId_);
    body.addQueryItem("client_secret", secretValue);
    body.addQueryItem("grant_type", "client_credentials");
    secretValue.fill(QChar(' '));
    QNetworkRequest request(QUrl("https://graph.facebook.com/oauth/access_token"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setTransferTimeout(20000);
    auto* reply = network_.post(request, body.query(QUrl::FullyEncoded).toUtf8());
    status_->setText("Requesting App Access Token from Meta...");
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        const QString token = response.value("access_token").toString();
        if (reply->error() == QNetworkReply::NoError && !token.isEmpty()) {
            tokenInput_->setText(token);
            status_->setText("App Access Token retrieved. Save it explicitly to persist it. Upload permission is not guaranteed.");
        } else {
            const QString error = response.value("error").toObject().value("message").toString();
            status_->setText("Get token failed: " + (error.isEmpty() ? reply->errorString() : error).left(500));
        }
        reply->deleteLater();
    });
}

void UploadDialog::saveToken() {
    if (tokenInput_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Missing token", "Enter your Asset Upload Access Token.");
        return;
    }
    if (storeToken(tokenInput_->text().trimmed()))
        QMessageBox::information(this, "Token saved", "Token stored in Windows Credential Manager.");
    else
        QMessageBox::warning(this, "Token storage failed", "Could not save token securely.");
}

bool UploadDialog::validateZip(const QString& file, QString& error) const {
    if (!QFileInfo(file).isFile()) { error = "ZIP file does not exist."; return false; }
    const QString script = QString::fromLatin1(R"PS(
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z=[System.IO.Compression.ZipFile]::OpenRead($env:FBIL_ZIP_PATH)
try {
    $names=@($z.Entries | ForEach-Object { $_.FullName.Replace('\','/') })
    if ($names -notcontains 'index.html') { throw 'index.html must be at archive root' }
    if ($names -notcontains 'fbapp-config.json') { throw 'fbapp-config.json must be at archive root' }
    foreach ($name in $names) {
        if ($name -match '(^/|(^|/)\.\.(/|$)|^[A-Za-z]:)' -or $name -match '(^|/)(cert\.pem|key\.pem|.*\.key)$') {
            throw "Unsafe or private file in bundle: $name"
        }
    }
} finally { $z.Dispose() }
)PS");
    return runPowerShell(script, file, error);
}

static QString packageDirectory(const QString& buildPath, const QString& tempPath, QString& error) {
    const QDir build(buildPath);
    if (!QFileInfo(build.filePath("index.html")).isFile() ||
        !QFileInfo(build.filePath("fbapp-config.json")).isFile()) {
        error = "Build folder must contain index.html and fbapp-config.json.";
        return {};
    }
    const QString stage = QDir(tempPath).filePath("bundle");
    QDir().mkpath(stage);
    QDirIterator iterator(buildPath, QDir::Files | QDir::NoSymLinks | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString source = iterator.next();
        const QString relative = build.relativeFilePath(source).replace('\\', '/');
        const QString name = QFileInfo(source).fileName().toLower();
        if (name == "cert.pem" || name == "key.pem" || name.endsWith(".key") ||
            name.endsWith(".zip") || relative.startsWith("../")) continue;
        const QString target = QDir(stage).filePath(relative);
        if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(source, target)) {
            error = "Cannot stage: " + relative;
            return {};
        }
    }
    const QString zip = QDir(tempPath).filePath("bundle.zip");
    const QString script = QString::fromLatin1(R"PS(
$ErrorActionPreference='Stop'
Compress-Archive -Path (Join-Path $env:FBIL_STAGE_PATH '*') -DestinationPath $env:FBIL_ZIP_PATH -Force
)PS");
    if (!runPowerShell(script, zip, error, stage)) {
        error = "ZIP creation failed: " + error;
        return {};
    }
    return zip;
}
void UploadDialog::setBusy(bool busy) {
    uploadButton_->setEnabled(!busy);
    existing_->setEnabled(!busy);
    pack_->setEnabled(!busy);
}
void UploadDialog::beginUpload() {
    if (!QRegularExpression("^[0-9]+$").match(appId_).hasMatch()) {
        QMessageBox::warning(this, "Invalid ID", "Facebook App ID must be numeric."); return;
    }
    const QString token = tokenInput_->text().trimmed();
    if (token.isEmpty()) { QMessageBox::warning(this,"Missing token","Provide an Asset Upload Access Token."); return; }
    if (pack_->isChecked()) {
        const QDir build(buildPath_);
        if (!QFileInfo(build.filePath("index.html")).isFile() ||
            !QFileInfo(build.filePath("fbapp-config.json")).isFile()) {
            status_->setText("Build folder must contain index.html and fbapp-config.json.");
            return;
        }
        temporary_ = std::make_unique<QTemporaryDir>();
        if (!temporary_->isValid()) {
            status_->setText("Cannot create temporary directory.");
            return;
        }
        packaging_ = true;
        setBusy(true);
        progress_->setRange(0, 0);
        status_->setText("Packaging build directory... UI remains responsive.");
        const QString sourceDir = buildPath_;
        const QString tmpDir = temporary_->path();
        auto* watcher = new QFutureWatcher<QPair<QString, QString>>(this);
        connect(watcher, &QFutureWatcher<QPair<QString, QString>>::finished, this,
            [this, watcher] {
                const auto result = watcher->result();
                watcher->deleteLater();
                progress_->setRange(0, 100);
                progress_->setValue(0);
                setBusy(false);
                if (result.first.isEmpty()) {
                    status_->setText("Packaging failed: " + result.second);
                    return;
                }
                uploadPreparedBundle(result.first);
            });
        watcher->setFuture(QtConcurrent::run([sourceDir, tmpDir] {
            QString error;
            QString zip = packageDirectory(sourceDir, tmpDir, error);
            return qMakePair(zip, error);
        }));
        return;
    }
    uploadPreparedBundle(zipPath_->text().trimmed());
}

void UploadDialog::uploadPreparedBundle(const QString& zip) {
    const QString token = tokenInput_->text().trimmed();
    if (token.isEmpty()) {
        QMessageBox::warning(this, "Missing token", "Provide an Asset Upload Access Token.");
        return;
    }
    QString validationError;
    if (!validateZip(zip, validationError)) {
        QMessageBox::warning(this, "Invalid bundle", validationError);
        return;
    }
    const QString confirmation = QString("Upload %1 to Meta Web Hosting for App ID %2?\n\n"
        "This action will NOT publish to Production.").arg(QFileInfo(zip).fileName(), appId_);
    if (QMessageBox::question(this, "Confirm upload", confirmation) != QMessageBox::Yes) return;
    if (!storeToken(token)) {
        QMessageBox::warning(this, "Credential error", "Token must be stored securely before uploading.");
        return;
    }
    auto* multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    auto field = [multipart](const QByteArray& name, const QByteArray& value) {
        QHttpPart p;
        p.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QVariant("form-data; name=\"" + QString::fromLatin1(name) + "\""));
        p.setBody(value);
        multipart->append(p);
    };
    field("access_token", token.toUtf8());
    field("type", "BUNDLE");
    field("comment", commentInput_->text().toUtf8());
    auto* data = new QFile(zip, multipart);
    if (!data->open(QIODevice::ReadOnly)) {
        delete multipart;
        QMessageBox::warning(this, "Read error", "Cannot open ZIP file.");
        return;
    }
    QHttpPart archive;
    archive.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant("form-data; name=\"asset\"; filename=\"bundle.zip\""));
    archive.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-stream"));
    archive.setBodyDevice(data);
    multipart->append(archive);
    QNetworkRequest request(QUrl("https://graph-video.facebook.com/" + appId_ + "/assets"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(180000);
    setBusy(true);
    status_->setText("Uploading bundle to Meta Web Hosting...");
    progress_->setValue(0);
    auto* reply = network_.post(request, multipart);
    currentReply_ = reply;
    multipart->setParent(reply);
    connect(reply, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
        if (total > 0) progress_->setValue(static_cast<int>(sent * 100 / total));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const auto body = reply->readAll();
        const auto parsed = QJsonDocument::fromJson(body).object();
        const auto failure = parsed.value("error").toObject();
        const bool ok = reply->error() == QNetworkReply::NoError &&
                        !parsed.isEmpty() && parsed.value("success").toBool(false);
        currentReply_ = nullptr;
        setBusy(false);
        if (ok) {
            progress_->setValue(100);
            status_->setText("Uploaded successfully. Check Web Hosting for processing / Standby. Production unchanged.");
            QMessageBox::information(this, "Upload completed",
                "Bundle sent to Meta Web Hosting. Verify status in the Meta dashboard. Production was NOT updated.");
            QDesktopServices::openUrl(QUrl("https://developers.facebook.com/apps/" + appId_ + "/instant-games/hosting/"));
        } else {
            QString reason = failure.value("message").toString();
            if (reason.isEmpty()) reason = reply->errorString();
            status_->setText("Upload failed (HTTP " + QString::number(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()) + "): " + reason.left(500));
        }
        reply->deleteLater();
    });
}
