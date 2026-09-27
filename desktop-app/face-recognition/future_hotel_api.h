#ifndef FUTURE_HOTEL_API_H
#define FUTURE_HOTEL_API_H

#include <QHostInfo>
#include <QHostAddress>
#include <QTcpSocket>
//#include <QSerialPort>
//#include <QSerialPortInfo>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>

#include <QThread>
#include <QTimer>

#include <iostream>
#include <stdio.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <assert.h>

int Serial_Init(const char *devpath, int baudrate);

class Future_Hotel : public QThread{

    Q_OBJECT
    public:

    QTcpSocket* bafa_socket;          //定义巴法云通信套接子
    QTcpSocket* xinzhi_socket;        //定义心知天气通信套接子
    QString BF_UID;                   //定义巴法云UID
    QString XZ_UID;                   //定义心知天气UID
    QString City;

    QString w_city;                     //城市
    QString w_teml;                     //温度下限
    QString w_temh;                     //温度上限
    QString w_hum;                      //湿度
    QString w_stat;                     //天气状态

//    QSerialPort * z_port;          //串口

    QTcpSocket* Connect_Server(QString hostname,int port);
    QString Get_Time(QTcpSocket* socket);
    QList<QString> Get_Weather(QTcpSocket* socket);

//    QSerialPort *Open_Serial(QString serialport,int baudrate);

    //构造函数
    Future_Hotel();
    //析构函数
    ~Future_Hotel();
    void run() override;    //线程重写函数
private slots:
    void Time_Out(void);


};

#endif // FUTURE_HOTEL_API_H
