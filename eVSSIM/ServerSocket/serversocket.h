#ifndef SERVERSOCKET_H
#define SERVERSOCKET_H

#include <QMainWindow>
#include <QDebug>
#include <QTcpServer>
#include <QTcpSocket>

namespace Ui {
class ServerSocket;
}

class ServerSocket : public QMainWindow
{
    Q_OBJECT

public:
    explicit ServerSocket(QWidget *parent = 0);
    ~ServerSocket();

private:
    Ui::ServerSocket *ui;
    QTcpServer *server;
    QTcpSocket *socket;
    bool clientConnected;
    bool collectData;

public slots:
    void newConnection();
private slots:
    void on_btnSend_clicked();
};


#endif // SERVERSOCKET_H
