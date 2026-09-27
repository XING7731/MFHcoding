#include "lldk.h"
#include "ui_lldk.h"
#include "QTimer"
#include "QDebug"
#include "QTime"
#include "stdio.h"
#include "string.h"
#include "QDate"
#include "jx_uart_send.h"
#include <QAbstractSocket>
int serial1 = 0;//用于操作UART2
int serial2 = 0;//用于操作UART3
QString serial_data;
int temp=0, humi=0;//保存温湿度
int smoke = 0;//保存烟雾
int tem_l =0, tem_h = 0, hum = 0;//存放室外温度和湿度
//int light = 0;//光照值
int jsq_state = 0;//加湿器的状态
int jsqflage = 0;
int mode = 0;
char buf[20] = {0};
lldk::lldk(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::lldk)
{
    ui->setupUi(this);
    QTimer *mytimer = new QTimer();
    connect(mytimer,&QTimer::timeout,this,&lldk::xyd_timer_out);
    mytimer->start(1000);

    connect(&w,SIGNAL(back_lldk()),this,SLOT(change_ui()));
    hotel.xinzhi_socket = hotel.Connect_Server("api.seniverse.com",80);

    xyd_weather_get();   // 先获取一次天气，立即显示
    QTimer *weather_timer = new QTimer();
    connect(weather_timer,&QTimer::timeout,this,&lldk::xyd_weather_get);
    weather_timer->start(1000*60);
    //上报数据 -- 巴法云
    hotel.bafa_socket = hotel.Connect_Server("bemfa.com",8344);
    /*
     * Get_Time() 用于从巴法云服务器获取当前网络时间，
     * 并通过 date -s 命令校准开发板的 Linux 系统时间。
     * 界面上的时间并不是由 Get_Time() 直接显示的，
     * 而是由 xyd_timer_out() 每秒读取系统时间并刷新 QLabel。
     * 因此程序启动后先调用 Get_Time() 校准系统时间，
     * 后续界面就可以显示准确的实时时间。
     */
    // 连接成功信号，连上服务器之后立刻发送订阅

    if (hotel.bafa_socket != NULL &&
        hotel.bafa_socket->state() == QAbstractSocket::ConnectedState) {
        QString subCmd = QString("cmd=1&uid=%1&topic=SmartCtrl\r\n")
                             .arg(hotel.BF_UID);
        hotel.bafa_socket->write(subCmd.toUtf8());
        hotel.bafa_socket->waitForBytesWritten(1000);
        qDebug() << "bafa subscribed: " << subCmd;
    }


    QTimer *bafa_timer = new QTimer();
    connect(bafa_timer,&QTimer::timeout,this,&lldk::xyd_sensor_send);
    bafa_timer->start(1000*5);

    connect(hotel.bafa_socket, SIGNAL(readyRead()),
            this, SLOT(bafa_socket_read_slot()));
}

void lldk::xyd_weather_get(void)
{
    // 如果天气服务器连接已断开，先重新连接
    if (hotel.xinzhi_socket == NULL ||
        hotel.xinzhi_socket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << "xinzhi socket reconnecting...";
        hotel.xinzhi_socket = hotel.Connect_Server("api.seniverse.com", 80);
    }

    // 重连失败则退出，避免使用无效 socket
    if (hotel.xinzhi_socket == NULL ||
        hotel.xinzhi_socket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << "xinzhi socket is not connected";
        return;
    }

    // 获取天气信息
    QList<QString> info = hotel.Get_Weather(hotel.xinzhi_socket);

    // 天气数据无效时不刷新界面，避免把原来的内容清空
    if (info.size() < 4 || info[1].isEmpty()) {
        qDebug() << "weather info invalid";
        return;
    }

    // 显示城市、天气状态和温度范围
    ui->label_city->setText(info[0]);
    ui->label_stat->setText("天气：" + info[1]);
    ui->label_t->setText(info[2] + "~" + info[3] + "℃");
    ui->label_h->setText("湿度:" + info[3] + "%");
    tem_l= info[2].toInt();
    tem_h= info[3].toInt();
    hum = info[4].toInt();
    QString weather = info[1];
    if (weather.contains("晴"))
    {
        ui->label_weather_img->setPixmap(QPixmap(":/image/sunny.png"));
    }
    else if (weather.contains("多云"))
    {
        ui->label_weather_img->setPixmap(QPixmap(":/image/cloudy.png"));
    }
    else if (weather.contains("阴"))
    {
        ui->label_weather_img->setPixmap(QPixmap(":/image/overcast.png"));
    }
    else if (weather.contains("雨"))
    {
        ui->label_weather_img->setPixmap(QPixmap(":/image/rain.png"));
    }
    else if (weather.contains("雪"))
    {
        ui->label_weather_img->setPixmap(QPixmap(":/image/snow.png"));
    }
    else
    {
        ui->label_weather_img->setPixmap(QPixmap(":/image/deafault.png"));
    }

    ui->label_weather_img->setScaledContents(true);
}
void lldk::xyd_sensor_send(void)
{
    if (hotel.bafa_socket == NULL ||
            hotel.bafa_socket->state() != QAbstractSocket::ConnectedState) {
            qDebug() << "bafa socket not connected, skip send";
            return;
        }
    QString cmd = QString("cmd=2&uid=%1&topic=Smart&msg=#%2#%3#%4#%5\r\n")
                .arg(hotel.BF_UID)
                .arg(temp)
                .arg(humi)
                .arg(smoke)
                .arg(jsqflage);
      hotel.bafa_socket->write(cmd.toUtf8());

}
void lldk::xyd_timer_out(void)
{
    QTime nowtim = QTime::currentTime();
    QDate nowdate = QDate::currentDate();
    int year = nowdate.year();
    int month = nowdate.month();
    int day = nowdate.day();

    int hour = nowtim.hour();
    int minute  = nowtim.minute();
    int second  = nowtim.second();
    char buff[16] = {0};
    sprintf(buff,"%02d:%02d:%02d",hour,minute,second);
    ui->label_time->setText(buff);

    sprintf(buff,"%04d-%02d-%02d",year,month,day);
    ui->label_data->setText(buff);

    sprintf(buff,"温度:%d℃",temp);
    ui->label_t2->setText(buff);
    sprintf(buff,"湿度:%d%%",humi);
    ui->label_h2->setText(buff);
    if(mode == 1)
    {
        if(humi <= 30)
        {
            jsq_state = 1;
            strcpy(buf, "jo");
            write(serial1, buf, sizeof(buf));
            ui->pushButton_jsq->setIcon(QIcon(":/image/on.png"));
        }
        else if(humi >= 80)
        {
            jsq_state = 0;
            strcpy(buf, "jf");
            write(serial1, buf, sizeof(buf));
            ui->pushButton_jsq->setIcon(QIcon(":/image/off.png"));
        }
    }
    sprintf(buff,"烟雾:%d",smoke);
    ui->label_s->setText(buff);
    if(smoke >1800)
    {
        ui->label_s_2->setPixmap(QPixmap(":/image/smoke.png"));
    }
    else
    {
        ui->label_s_2->setPixmap(QPixmap(":/image/smoke2.png"));
    }
//    sprintf(buff,"光照:%d",light);
//    ui->label_l->setText(buff);
}
void lldk::change_ui()
{
    w.hide();
    this->show();
}
lldk::~lldk()
{
    delete ui;
}

void lldk::on_changeui_btn_clicked()
{
    this->hide();
    w.show();
}
void Serial1_Read::run()   //线程
{
   char recv[64] = {0};
    //用于UART2
    serial1 = Serial_Init("/dev/ttySAC2",9600);
    while(1)
    {
        memset(recv, 0, sizeof(recv));

        int len = read(serial1, recv, sizeof(recv) - 1);
        if (len > 0)
        {
            recv[len] = '\0';

            QString data = QString::fromLatin1(recv).trimmed();
            printf("zigbee send %s\n", recv);

            QStringList parts = data.split('#', QString::SkipEmptyParts);
            if (parts.count() >= 3)
            {
                temp = parts.at(0).toInt();
                humi = parts.at(1).toInt();
                smoke = parts.at(2).toInt();
                jsqflage = parts.at(3).toInt();
            }
        }
    }

}

void Serial2_Read::run()   //线程
{
    unsigned char cmd;
    //用于UART3
    serial2 = Serial_Init("/dev/ttySAC3",B9600);
    while(1)
    {
        int len = read(serial2, &cmd, 1);
        if (len == 1) {
            printf("cmd is %02x\n", cmd);
            switch(cmd)
            {
                case 0x01:
                    strcpy(buf,"jo");
                    write(serial1,buf,sizeof(buf));
                    break;//打开加湿器
                case 0x02:
                    strcpy(buf,"jf");
                    write(serial1,buf,sizeof(buf));
                    break;//关闭加湿器
                case 0x03:
                    strcpy(buf,"ro");
                    write(serial1,buf,sizeof(buf));
                    break;//开红灯
                case 0x04:
                    strcpy(buf,"go");
                    write(serial1,buf,sizeof(buf));
                    break;//开绿灯
                case 0x05:
                    strcpy(buf,"bo");
                    write(serial1,buf,sizeof(buf));
                    break;//开蓝灯
                case 0x06:
                    strcpy(buf,"on");
                    write(serial1,buf,sizeof(buf));
                    break;//开彩灯
                case 0x07:
                    strcpy(buf,"of");
                    write(serial1,buf,sizeof(buf));
                    break;//关灯
                case 0x08:_uart_Tem_In_Play(temp);break;//室内温度
                case 0x09:_uart_Tem_Out_Play(tem_l,tem_h);break;//室外温度
                case 0x0A:_uart_Hum_In_Play(humi);break;//室内湿度
                case 0x0B:_uart_Hum_Out_Play(hum);break;//室外湿度
                case 0x0C:_uart_Smoke_Play(smoke);break;//有毒气体浓度
                //case 0x0D:_uart_Light_Play(light);break;//光照值
//                case 0x0E:_uart_Person_Play(person);break;//当前有人吗

            }
        }
    }
}

void lldk::on_pushButton_jsq_clicked()
{
    if(mode == 0)
    {
        jsq_state = !jsq_state;
        if (jsq_state)
        {
            strcpy(buf, "jo");
            write(serial1, buf, sizeof(buf));
            printf("buf %s\n", buf);
            ui->pushButton_jsq->setIcon(QIcon(":/image/on.png"));
        }
        else
        {
            strcpy(buf, "jf");
            write(serial1, buf, sizeof(buf));
            printf("buf %s\n", buf);
            ui->pushButton_jsq->setIcon(QIcon(":/image/off.png"));
        }
    }
}

void lldk::on_mode_change_clicked()
{

    if (mode == 0) {
        mode = 1;
        ui->mode_change->setText("自动");
    } else {
        mode = 0;
        ui->mode_change->setText("手动");
    }
}

void lldk::on_pushButton_all_led_clicked()
{
    static int count = 0;
    char buf[20]={0};
    switch(count)
    {
        case 0:
            strcpy(buf,"ro");//开红灯
            write(serial1,buf,sizeof(buf));
            printf("buf %s\n", buf);
            ui->pushButton_all_led->setIcon(QIcon(":/image/red.png"));
        break;
        case 1:
            strcpy(buf,"yo");//开黄灯
            write(serial1,buf,sizeof(buf));
            printf("buf %s\n", buf);
            ui->pushButton_all_led->setIcon(QIcon(":/image/yellow.png"));
        break;
        case 2:
            strcpy(buf,"off");//关所有的灯
            write(serial1,buf,sizeof(buf));
            printf("buf %s\n", buf);
            ui->pushButton_all_led->setIcon(QIcon(":/image/null.png"));
        break;
    }
    count++;
    count%=3;
}


void lldk::bafa_socket_read_slot()
{
    QTcpSocket *sock = hotel.bafa_socket;
    if(!sock) return;

    static QString bafa_buf;   //静态缓冲区，处理粘包
    QByteArray raw = sock->readAll();
    qDebug()<<"[BAFA RAW]"<<raw; //打印所有收到的字节！！！
    bafa_buf += raw;

    while(bafa_buf.contains("\r\n"))
    {
        int pos = bafa_buf.indexOf("\r\n");
        QString one_pkg = bafa_buf.left(pos);
        bafa_buf = bafa_buf.mid(pos+2);

        qDebug()<<"巴法云完整报文:"<<one_pkg;

        //只有报文【开头】是cmd=1，才是服务器下发消息
        // 只处理 cmd=2 且含 msg= 的消息，跳过 cmd=2&res=1 的应答
        if( !one_pkg.startsWith("cmd=2") || one_pkg.contains("res=") || !one_pkg.contains("msg=") )
        {
            continue;
        }

        //后面截取msg_str解析逻辑不变
        int msg_pos = one_pkg.indexOf("msg=");
        if(msg_pos == -1) continue;
        QString msg_str = one_pkg.mid(msg_pos+4);

        QStringList parts = msg_str.split('#',QString::SkipEmptyParts);
        qDebug()<<"parts:"<<parts;

        if(parts.size() == 2)
        {
            int cloud_mode = parts.at(0).toInt();
            int cloud_jsq  = parts.at(1).toInt();
            qDebug()<<"cloud_mode:"<<cloud_mode<<" cloud_jsq:"<<cloud_jsq;

            //业务逻辑不变
            mode = cloud_mode;
            if(mode == 0){
                ui->mode_change->setText("手动");
            }else{
                ui->mode_change->setText("自动");
            }

            jsq_state = cloud_jsq;
            if(jsq_state == 1)
            {
                strcpy(buf,"jo");
                write(serial1,buf,sizeof(buf));
                ui->pushButton_jsq->setIcon(QIcon(":/image/on.png"));
            }else{
                strcpy(buf,"jf");
                write(serial1,buf,sizeof(buf));
                ui->pushButton_jsq->setIcon(QIcon(":/image/off.png"));
            }

        }
        else if(parts.size() == 1)
        {
            int cloud_led  = parts.at(0).toInt();
            char led_buf[20]={0};
            switch (cloud_led) {
            case 0:
                strcpy(led_buf,"off");
                write(serial1,led_buf,sizeof(led_buf));
                printf("led_buf %s\n", led_buf);
                ui->pushButton_all_led->setIcon(QIcon(":/image/null.png"));
                break;
            case 1:
                strcpy(led_buf,"ro");
                write(serial1,led_buf,sizeof(led_buf));
                printf("led_buf %s\n", led_buf);
                ui->pushButton_all_led->setIcon(QIcon(":/image/red.png"));
                break;
            case 2:
                strcpy(led_buf,"yo");
                write(serial1,led_buf,sizeof(led_buf));
                printf("led_buf %s\n", led_buf);
                ui->pushButton_all_led->setIcon(QIcon(":/image/yellow.png"));
                break;
            default:
                break;
            }
        }
    }
}

