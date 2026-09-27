#include "widget.h"
#include "ui_widget.h"
#include <QDebug>

#if FACE == 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


#endif

#if FACE == 1
VideoCapture capeture;
Ptr<LBPHFaceRecognizer> model;
RECOGNIZE p_recognize;

CascadeClassifier faceClassifier;

Mat resource;
Mat frame;
Mat gray;
Mat face_test;
Mat myface;

vector<Rect> faces;//存储检测到的人脸数据
int xx = 0,yy = 0;
QFile facemodel(QDir::currentPath() + "/facemodel.xml");
QFile user(QDir::currentPath() + "/usr.txt");

int face_count = 0;
int file_stat = 0;
int photo_flag = 0;
#endif

Ui::Widget *face_ui;


Widget::Widget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Widget)
{
    ui->setupUi(this);
    face_ui = ui;

    connect(&set,SIGNAL(backHome()),this,SLOT(on_backHome()));

#if FACE == 1
#if ARCH == 1
    capeture.open(0);
#elif ARCH == 2
    capeture.open(15);
#endif
    if(!capeture.isOpened()){
        qDebug()<<"打开摄像头失败";
    }

    //训练好的文件名称，放置在可执行文件同目录下
#if ARCH == 1
     bool flag =faceClassifier.load("/home/xyd/tools/opencv-3.4.5/data/haarcascades/haarcascade_frontalface_alt2.xml");

#elif ARCH == 2
    bool flag =faceClassifier.load("/usr/share/OpenCV/haarcascades/haarcascade_frontalface_alt2.xml");
#endif
    if(flag == false)
    {
        QMessageBox::warning(this,"Haar级联分类器错误","不能加载haarcascade_frontalface_alt2.xml文件！！",QMessageBox::Ok);
        return;
    }

    showVideo();

    show_time = new QTimer;
    show_time->setInterval(10); //设置定时器时间间隔
    connect(show_time,SIGNAL(timeout()),this,SLOT(showVideo()));
    show_time->start();

    recognize_time = new QTimer;
    connect(recognize_time,SIGNAL(timeout()),this,SLOT(start_Recognize()));
    recognize_time->start(1000);
#endif
}

Widget::~Widget()
{
#if FACE == 1
    capeture.release();
#endif
    delete ui;
}

#if FACE == 1
QString name;
void RECOGNIZE::run()
{
//    sleep(10);

    QString line;
    QStringList list;
    QString XmlPath = QDir::currentPath() + "/facemodel.xml";

    qint64 usrlinenum =0;
    int predictLabel = -1;
    double predictConfidence = 0.0;

    static int place_error = 0;
    static int success = 0;
    static int find_error = 0;

    string tex;
    while(1)
    {
rflag:

        if(myface.empty())
        {
            continue;
        }
        cv::resize(myface, face_test, cv::Size(92,112));//函数将图像myface调整为大小为92x112的图像，并存储在face_test中
                if(faces.size() == 0)//检测到的人脸个数
                {
                   face_ui->label_result->setText("请调整位置");
                   place_error++;
                   continue;
                }
        if (!face_test.empty())//用于检测数据框（DataFrame）是否为空的方法 如果返回 True，说明数据框没有任何数据；如果返回 False，则表示数据框中含有数据。
        {        
            predictLabel = -1;
            predictConfidence = 0.0;
            usrlinenum = 0;
            user.reset();//关闭打开的文件

            model = LBPHFaceRecognizer::create();//来创建一个LBPH（局部二值模式直方图）人脸识别模型
            model = Algorithm::load<LBPHFaceRecognizer>(XmlPath.toStdString());//加载一个训练好的LBPH人脸识别模型
            model->predict(face_test, predictLabel, predictConfidence);//使用某个模型进行预测，输入为 face_test（测试的面部图像），输出为 predictLabel（预测标签）和 predictConfidence（预测置信度）

            face_count++;
            if(predictConfidence > 85)
            {
                qDebug()<<"查无此人";
                face_ui->label_result->setText("查无此人");
                cv::putText(frame,"find error!!!",Point(xx,yy),cv::FONT_HERSHEY_SIMPLEX,1,Scalar(0, 0, 255),1,8,false);
                find_error++;
                continue;
            }

            QTextStream in(&user);//表示将输入流与一个设备（如文件或标准输入）连接。
            //in.atEnd() 方法用于检查流是否已到达末尾，返回一个布尔值，指示是否可以继续读取数据。
            while(!in.atEnd())
            {
                line = in.readLine();//获取当前行的数据
                usrlinenum++;
                qDebug() << "usrlinenum is "<<usrlinenum;
                if(usrlinenum == predictLabel)
                {
                    list = line.split(";");//将读取的行按照分号进行分割，生成一个字符串列表
                    name = list[1];
                    qDebug() << list[1];
                    tex = name.toStdString();
                    cv::putText(frame,tex,Point(xx,yy),cv::FONT_HERSHEY_SIMPLEX,1,Scalar(0, 0, 255),1,8,false);
                    face_ui->label_result->setText(QString("识别结果：")+list[1]);
                    success++;
                    goto rflag;
                }
            }
        }
        else
        {
            face_ui->label_result->setText("没有检测到人脸");
            usleep(10000);
        }
    }
    user.close();
    facemodel.close();
}
#endif

#if FACE == 1
QImage Widget::Mat2Image(Mat image)
{
     cvtColor(image,image,CV_BGR2RGB);
     QImage src = QImage((const unsigned char *)(image.data),image.cols,image.rows,image.step,QImage::Format_RGB888);
     return src;
}
#endif

#if FACE == 1
int infor = 0;
void Widget::start_Recognize()
{
    if(file_stat == 0)
    {
        bool ok = facemodel.open(QIODevice::ReadOnly | QIODevice::Text);
        if(!ok)
        {
            if(infor == 0)
            {
                infor = 1;
            }
            qDebug() << facemodel.errorString() <<endl;
            qDebug("facemodel.xml error");
        }
        else
        {
            file_stat++;//文件存在
        }
    }
    else if(file_stat == 1)
    {
        if(!user.open(QIODevice::ReadWrite | QIODevice::Text))
        {
            qDebug() << user.errorString() <<endl;
            qDebug("usr.xml error");
        }
        else
        {
            file_stat++;//文件存在
        }
    }
    if(file_stat == 2)
    {
        p_recognize.start();//线程运行
        recognize_time->stop();
    }
}

void Widget::showVideo()
{
     capeture >> resource;

     frame = resource.clone();

     //灰度转换
     cvtColor(frame, gray, CV_BGR2GRAY);
     equalizeHist(gray, gray);

     // 1. 原始图像检测
     faceClassifier.detectMultiScale(gray, faces, 1.4,3,0, cv::Size(90, 112));
     // 2. 水平翻转检测 (对侧面倾斜人脸有效，只多一次检测)
     Mat flipped;
     flip(gray, flipped, 1);  // 水平翻转
     vector<Rect> flippedFaces;
     faceClassifier.detectMultiScale(gray, faces, 1.4,3,0, cv::Size(90, 112));
     // 将翻转后的坐标转换回来
     for (const Rect& rect : flippedFaces) {
         Rect newRect(gray.cols - rect.x - rect.width, rect.y, rect.width, rect.height);
         faces.push_back(newRect);
     }

     // 合并重叠的检测框
     if (faces.size() > 1) {
         vector<Rect> merged;
         vector<bool> used(faces.size(), false);

         for (size_t i = 0; i < faces.size(); i++) {
             if (used[i]) continue;
             Rect current = faces[i];
             for (size_t j = i + 1; j < faces.size(); j++) {
                 if (used[j]) continue;
                 Rect inter = current & faces[j];
                 if (inter.area() > 0) {
                     float overlapRatio = (float)inter.area() / min(current.area(), faces[j].area());
                     if (overlapRatio > 0.3) {
                         current.x = min(current.x, faces[j].x);
                         current.y = min(current.y, faces[j].y);
                         current.width = max(current.x + current.width, faces[j].x + faces[j].width) - current.x;
                         current.height = max(current.y + current.height, faces[j].y + faces[j].height) - current.y;
                         used[j] = true;
                     }
                 }
             }
             merged.push_back(current);
             used[i] = true;
         }
         faces = merged;
     }

     for (size_t i = 0; i < faces.size(); i++)
     {
         if (faces[i].height > 0 && faces[i].width > 0)
         {
             myface = gray(faces[i]);
             rectangle(frame, faces[i], cv::Scalar(0, 0, 255), 1, 8, 0);
             xx = faces[i].x;
             yy = faces[i].y;
         }
     }
     QImage res = Mat2Image(frame);
     ui->label->setPixmap(QPixmap::fromImage(res));
}

#endif

void Widget::on_backHome()
{
    set.hide();
    this->show();
}


void Widget::on_ButtonSet_clicked()
{
    this->hide();
    set.show();
}


void Widget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPixmap pixmap = QPixmap("://image/4.png").scaled(this->size());
    QPainter painter(this);
    painter.drawPixmap(this->rect(),pixmap);
}

void Widget::pushButton_back_mainWindow_clicked()
{
//    emit back_mainWindow();
}

void Widget::on_pushButton_clicked()
{
    emit back_lldk();
}
