#ifndef SENSOR_H
#define SENSOR_H

#include <QWidget>

namespace Ui {
class Sensor;
}

class Sensor : public QWidget
{
    Q_OBJECT

public:
    explicit Sensor(QWidget *parent = 0);
    ~Sensor();
signals:
    void sensor_back_lldk();
private slots:
    void on_pushButton_clicked();
    void sensor_timer_out(void);
private:
    Ui::Sensor *ui;
};

#endif // SENSOR_H
