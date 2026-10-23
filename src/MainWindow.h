#pragma once
#include <QMainWindow>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QUrl>
#include <QNetworkAccessManager>
class QTableWidget;
class QLabel;
class QPushButton;
class QLineEdit;
class QSpinBox;
class QUrl;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
private:
    QJsonArray games_;
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QProcess* server_ = nullptr;
    QString activePath_;
    int activePort_ = -1;
    QTimer* serverReadyTimer_ = nullptr;
    QUrl pendingUrl_;
    int readinessAttempts_ = 0;
    QString serverDiagnostics_;
    void load();
    void save();
    void refresh();
    int selectedIndex() const;
    void addGame();
    void editGame();
    void removeGame();
    void playGame();
    void stopServer();
    void openDataFile();
    void showDependencies();
    void configureUpdates();
    void checkForUpdates(bool manual = false);
    void downloadUpdate(const QString& tag, const QUrl& assetUrl);
    QString updateRepository() const;
    QNetworkAccessManager network_;
    bool editDialog(QJsonObject& obj, const QString& title);
    static QString dataFile();
};
