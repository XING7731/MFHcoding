#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include "setting.h"
#include <QPainter>
#include <QTimer>
#include <QThread>


#define ALL_FUNC 1
#define ARCH_ARM 2

#define FACE ALL_FUNC


#define ARCH ARCH_ARM

#if FACE == 1
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>

#include "face/facerec.hpp"
#include "face.hpp"

using namespace cv;
using namespace std;
using namespace face;
#endif
namespace Ui {
class Widget;
}

class RECOGNIZE : public QThread{
public:
    void run() override;
};



class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = 0);
    ~Widget();
    Setting set;
    QTimer *show_time;
    QTimer *recognize_time;
#if FACE == 1
    QImage Mat2Image(Mat image);
#endif
    void paintEvent(QPaintEvent *event);
signals:
    void back_mainWindow();
    void back_lldk();
private slots:
#if FACE == 1
    void showVideo();
    void start_Recognize();
#endif
    void on_backHome();
    void on_ButtonSet_clicked();

    void pushButton_back_mainWindow_clicked();

    void on_pushButton_clicked();

private:
    Ui::Widget *ui;
};

#endif // WIDGET_H
