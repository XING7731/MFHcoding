#ifndef LLDK_H
#define LLDK_H

#include <QWidget>
#include "widget.h"
#include "future_hotel_api.h"

#include <QRegularExpression>

extern int serial2;

namespace Ui {
class lldk;
}

class lldk : public QWidget
{
    Q_OBJECT

public:
    explicit lldk(QWidget *parent = 0);
    Future_Hotel hotel;
    Widget w;
    ~lldk();

private slots:
    void xyd_sensor_send(void);
    void xyd_weather_get(void);
    void xyd_timer_out(void);
    void on_changeui_btn_clicked();
    void change_ui();

    void on_pushButton_jsq_clicked();

    void on_mode_change_clicked();

    void on_pushButton_all_led_clicked();

    void bafa_socket_read_slot();   //巴法云接收槽

private:
    Ui::lldk *ui;
};

class Serial1_Read : public QThread{
public:
    void run() override;
};
class Serial2_Read : public QThread{
public:
    void run() override;
};
#endif // LLDK_H
