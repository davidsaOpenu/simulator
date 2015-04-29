#include "serversocket.h"
#include "ui_serversocket.h"

ServerSocket::ServerSocket(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ServerSocket)
{
    ui->setupUi(this);

    server = new QTcpServer(this);
    clientConnected = false;
    collectData = true;
    connect(server,SIGNAL(newConnection()),this,SLOT(newConnection()));

    if(!server->listen(QHostAddress::Any,1234))
    {
        qDebug() << "Server could not start!";
    }else{
        qDebug() << "Server started";
    }


}

ServerSocket::~ServerSocket()
{
    socket->close();
    delete ui;
}

void ServerSocket::newConnection()
{
    socket = server->nextPendingConnection();
    clientConnected = true;

    ui->lblReceiveText->setText("Connection Found");
}

void ServerSocket::on_btnSend_clicked()
{
    if (clientConnected){
        if (collectData == true){
            socket->write("start");
            collectData = false;
            ui->btnSend->setText("Stop Collecting");
        }else{
            socket->write("stop");
            collectData = true;
            ui->btnSend->setText("Start Collecting");
        }
        socket->flush();
        socket->waitForBytesWritten(3000);
    } else{
        ui->lblReceiveText->setText("No Connection Found");
    }
}
