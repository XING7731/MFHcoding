#include "sensor.h"
#include "ui_sensor.h"
#include "QTimer"
Sensor::Sensor(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Sensor)
{
    ui->setupUi(this);
    QTimer *sensor_timer = new QTimer();
    connect(sensor_timer,&QTimer::timeout,this,&Sensor::sensor_timer_out);
    sensor_timer->start(1000);
}

Sensor::~Sensor()
{
    delete ui;
}

void Sensor::on_pushButton_clicked()
{
    emit sensor_back_lldk();
}
void Sensor::sensor_timer_out()
{
        sprintf(buf,"温度:%02d℃",temp);
        ui->label_t2->setText(buf);
        sprintf(buf,"湿度:%02d%%",humi);
        ui->label_h->setText(buf);
        sprintf(buf,"烟雾:%04d",smoke);
        ui->label_s->setText(buf);
}
