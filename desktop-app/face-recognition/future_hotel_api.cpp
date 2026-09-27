#include "future_hotel_api.h"
//#include "mainwindow.h"
#include "lldk.h"

Future_Hotel::Future_Hotel(void)
{
  
    BF_UID = "2104f24ee9ac8fd866b50759b430c08d";//巴法云私钥
    XZ_UID = "SUvt9lR6r2EfbsZUA";    //心知天气密钥
    City = "zhengzhou";               //天气目标城市
    //启动UART3的线程
    Serial2_Read *s2 = new Serial2_Read();
    s2->start();
    //启动UART2的线程
    Serial1_Read *s1 = new Serial1_Read();
    s1->start();
}

void Future_Hotel::Time_Out(void)
{
    qDebug() << "this is timer";
}

void Future_Hotel::run(void)
{
    sleep(5);
//    char num = 0;
//    char recv[16] = {0};
//    qDebug() << "This is run";
//    while(1)
//    {
//        num++;
//        printf("num is %d\n",num);
//        // 发送数据
//        write(serfd,&num,sizeof(num));
//        if(num == 3)
//        {
//            read(serfd,recv,sizeof(recv));
//            for(int i=0;i<16;i++)
//                printf("%d ",recv[i]);
//            printf("\n");
//            num = 0;
//        }

//        sleep(5);
//    }
}

QTcpSocket* Future_Hotel::Connect_Server(QString hostname,int port)
{
    QTcpSocket *socket = new QTcpSocket();
    QString Host_Addr;          //定义域名解析后的IP
    QHostInfo info = QHostInfo::fromName(hostname); //解析域名

    foreach (const QHostAddress &address, info.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                Host_Addr = address.toString();     //获取IPV4地址
                // qDebug() << "IPv4 address: " << address.toString();
                }
            }

    socket->connectToHost(Host_Addr, port);     // 连接到服务器，需要提供IP地址和端口
    if (socket->waitForConnected(5000)) { // 等待连接建立，超时时间5秒
        qDebug() << "Connected to "<<hostname<<" Cloud!";
        return socket;
    } else {
        qDebug() << "Connection failed!";
    }
    return NULL;
}

QString Future_Hotel::Get_Time(QTcpSocket* socket)
{
    QString cmd = "cmd=7&uid="+BF_UID+"&type=1\r\n";
    socket->write(cmd.toUtf8());
    socket->waitForBytesWritten(3000);   // 等待数据发送完成，超时时间3秒
    socket->waitForReadyRead(3000);      // 等待响应数据到达，超时时间3秒
    QString response = socket->readAll();// 接收响应数据

    char buf[32] = {0};
    QByteArray ba = response.toUtf8();
    char *p = ba.data();
    sprintf(buf,"date -s \"%s\"",p);
    system(buf);

    return response;
}

QList<QString> Future_Hotel::Get_Weather(QTcpSocket* socket)
{
    QString cmd = "GET https://api.seniverse.com/v3/weather/daily.json?key="+XZ_UID\
                    +"&location="+City+"&language=zh-Hans&unit=c&start=0&days=2\r\n";
    socket->write(cmd.toUtf8());
    socket->waitForBytesWritten(3000);   // 等待数据发送完成，超时时间3秒
    socket->waitForReadyRead(3000);      // 等待响应数据到达，超时时间3秒
    QString response = socket->readAll();// 接收响应数据
    /*数据解析开始*/
    QJsonDocument jsonDoc = QJsonDocument::fromJson(response.toUtf8());

    if (!jsonDoc.isNull() && jsonDoc.isObject()) {
        QJsonObject rootObj = jsonDoc.object();

        if (rootObj.contains("results") && rootObj["results"].isArray() && !rootObj["results"].toArray().isEmpty())
        {
            QJsonObject resultObj = rootObj["results"].toArray().at(0).toObject();

            if (resultObj.contains("location") && resultObj.contains("daily")) {

                QJsonObject locationObj = resultObj["location"].toObject();
                QJsonArray dailyArray = resultObj["daily"].toArray();
                QJsonObject dailyObj = dailyArray.at(0).toObject();

                w_city = locationObj["name"].toString();    //城市
//                qDebug() << w_city;

                if(!dailyArray.isEmpty())
                {
                    w_stat = dailyObj["text_day"].toString();   //天气状态
                    w_temh = dailyObj["high"].toString();       //温度上限
                    w_teml = dailyObj["low"].toString();        //温度下限
                    w_hum = dailyObj["humidity"].toString();    //湿度

//                    qDebug() << w_stat;qDebug() << w_teml;qDebug() << w_temh;qDebug() << w_hum;
                }
            }
        }
    }
    QList<QString> info;
    info<<w_city<<w_stat<<w_teml<<w_temh<<w_hum;
    //info[0]:城市,info[1]:天气,info[2]:最低温度,info[3]:最高温度,info[4]:湿度
    return info;
}

//QSerialPort *Future_Hotel::Open_Serial(QString serialport,int baudrate)
//{
//    QSerialPort *serialPort = new QSerialPort;
//    // 寻找并选择串口
//    foreach (const QSerialPortInfo &info, QSerialPortInfo::availablePorts()) {
//        qDebug() << info.portName();
//        if (info.portName() == serialport) {  // 假设我们要打开的串口是COM1
//            serialPort->setPort(info);
//            break;
//        }
//    }
//    // 配置串口参数
//    serialPort->setBaudRate(QSerialPort::Baud9600);
//    serialPort->setDataBits(QSerialPort::Data8);
//    serialPort->setParity(QSerialPort::NoParity);
//    serialPort->setStopBits(QSerialPort::OneStop);
//    serialPort->setFlowControl(QSerialPort::NoFlowControl);

//    // 打开串口
//    if (!serialPort->open(QIODevice::ReadWrite)) {
//        qDebug() << "Error opening serial port";
//        return NULL;
//    }
//}

Future_Hotel::~Future_Hotel(void)
{
//    if(bafa_socket->isValid())               // 判断套接子是否连接
//        bafa_socket->disconnectFromHost();   // 断开连接
//    if(xinzhi_socket->isValid())             // 判断套接子是否连接
//        xinzhi_socket->disconnectFromHost(); // 断开连接
    if (bafa_socket != NULL && bafa_socket->isValid())
            bafa_socket->disconnectFromHost();

        if (xinzhi_socket != NULL && xinzhi_socket->isValid())
            xinzhi_socket->disconnectFromHost();
}

int Serial_Init(const char *devpath, int baudrate) //串口初始化代码
{
    int fd;
    struct termios oldtio, newtio;

    assert(devpath != NULL);

    fd = open(devpath, O_RDWR | O_NOCTTY);
    if (fd == -1) {
        perror("serial->open");
        return -1;
    }

    tcgetattr(fd, &oldtio);		/* save current port settings */

    memset(&newtio, 0, sizeof(newtio));
    newtio.c_cflag = baudrate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    newtio.c_lflag = 0;		/* set input mode (non-canonical, no echo,...) */

    newtio.c_cc[VTIME] = 1;	/* set timeout value, n * 0.1 S */
    newtio.c_cc[VMIN] = 36;

    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &newtio);

    return fd;
}
