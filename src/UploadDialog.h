#include <memory>
#pragma once
#include <QDialog>
#include <QNetworkAccessManager>
#include <QPointer>
class QLineEdit;
class QLabel;
class QRadioButton;
class QProgressBar;
class QPushButton;
class QNetworkReply;
class QTemporaryDir;
class UploadDialog : public QDialog {
    Q_OBJECT
public:
    UploadDialog(const QString& gameName, const QString& appId, const QString& buildPath, QWidget* parent = nullptr);
private:
    void chooseZip();
    void saveToken();
    void beginUpload();
    void setBusy(bool busy);
    QString readToken() const;
    bool storeToken(const QString& token);
    void uploadPreparedBundle(const QString& zip);
    bool validateZip(const QString& file, QString& error) const;
    QString gameName_, appId_, buildPath_;
    QLineEdit *zipPath_=nullptr, *tokenInput_=nullptr, *commentInput_=nullptr;
    QRadioButton *existing_=nullptr, *pack_=nullptr;
    QLabel *status_=nullptr;
    QProgressBar *progress_=nullptr;
    QPushButton *uploadButton_=nullptr;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> currentReply_;
    bool packaging_ = false;
    std::unique_ptr<QTemporaryDir> temporary_;
};
