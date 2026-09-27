#ifndef SETTING_H
#define SETTING_H

#include <QWidget>
#include <QTimer>
#include <QDebug>


#define ALL_FUNC 1
#define ARCH_ARM 2   //1是电脑2是开发板

#define FACE ALL_FUNC
#define ARCH ARCH_ARM

#if FACE == 1
#include "/home/xyd/tools/opencv-3.4.5/opencv_contrib-3.4.5/modules/face/include/opencv2/face.hpp"
#include "/home/xyd/tools/opencv-3.4.5/opencv_contrib-3.4.5/modules/face/include/opencv2/face/facerec.hpp"


#include </usr/local/include/opencv2/face.hpp>
#include "/usr/local/include/opencv2/face/facerec.hpp"

#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/core.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include <iostream>
#include "opencv2/imgproc/types_c.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>

using namespace cv;
using namespace std;
using namespace face;
#endif

#include <QImage>
#include <QMessageBox>
#include <QDir>
#include <QTime>
#include <QTimer>
#include <QPainter>
#include <QFile>

namespace Ui {
class Setting;
}

class Setting : public QWidget
{
    Q_OBJECT

public:
    explicit Setting(QWidget *parent = 0);
    ~Setting();
    QTimer *time;
    int count = 0;
#if FACE == 1
    Ptr<LBPHFaceRecognizer> model;
#endif
    void Sleep(int msec);
    void paintEvent(QPaintEvent *event);
signals:
    void backHome();
private slots:
    void on_display();
    void on_ButtonTakePic_clicked();
    void on_ButtonBack_clicked();

    void on_ButtonCsv_clicked();

    void on_ButtonTrain_clicked();

//    void on_lineEdit_name_textChanged(const QString &arg1);

private:
    Ui::Setting *ui;
};

#endif // SETTING_H
