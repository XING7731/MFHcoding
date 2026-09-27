#include "setting.h"
#include "ui_setting.h"

#if FACE == 1
extern Mat resource;
#endif

Setting::Setting(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Setting)
{
    ui->setupUi(this);
    time = new QTimer;
    time->setInterval(30);
    time->start();
    connect(time,SIGNAL(timeout()),this,SLOT(on_display()));

    //填充下拉框
    ui->comboBox->addItem("xyd");
    ui->comboBox->addItem("bsz");
}

Setting::~Setting()
{
    delete ui;
}
void Setting::on_display()
{
#if FACE == 1
    if(!resource.empty()){
        Mat dst = resource.clone();
        cvtColor(dst,dst,CV_BGR2RGB);
        QImage src = QImage((const unsigned char *)(dst.data),dst.cols,dst.rows,dst.step,QImage::Format_RGB888);
        ui->label->setPixmap(QPixmap::fromImage(src));
    }
#endif
}


void Setting::on_ButtonTakePic_clicked()
{
    if(ui->comboBox->currentIndex() < 0)
    {
        QMessageBox::warning(this,"信息录入","请选择姓名",QMessageBox::Ok);
    }
//    if(ui->lineEdit_name->text().isEmpty())
//    {
//        QMessageBox::warning(this,"信息录入","请输入姓名",QMessageBox::Ok);
//    }
    else
    {
#if FACE == 1
         if(resource.empty())
         {
             QMessageBox::warning(this,"摄像头没有画面", "请检查摄像头是否正常", QMessageBox::Ok);
             return;
         }

         QDir *path = new QDir(QDir::currentPath() + "/TEMP");
         bool exist = path->exists();
         if(!exist)
             path->mkdir(QDir::currentPath() + "/TEMP");


         QString filename =  QDir::currentPath() + QString("/TEMP/%1.jpg").arg(count);
         ui->ButtonTakePic->setText(QString("拍照")+QString::number(count+1,10));
         Mat photo = resource.clone();
         imwrite(filename.toStdString(), photo);
         count++;
#endif
    }
}

void Setting::on_ButtonBack_clicked()
{
    count = 0;
    emit backHome();
}
void Setting::Sleep(int msec)
{
    QTime dieTime = QTime::currentTime().addMSecs(msec);
       while( QTime::currentTime() < dieTime )
           QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}
void Setting::on_ButtonCsv_clicked()
{
    ui->ButtonTakePic->setText("");
    count = 0;
    QMessageBox *startSamplingBox = new QMessageBox(QMessageBox::Information,"信息","开始取样请稍后！！！",QMessageBox::NoButton,this);
#if FACE == 1
     startSamplingBox->show();
     //Sleep(10);
     /********向文件添加一行信息 源文件行数+姓名  usr.txt 统计系统录入的人脸类型个数信息********/
     QFile user(QDir::currentPath() + "/usr.txt");//currentPath() 当前工作的目录
     if(!user.open(QIODevice::ReadWrite | QIODevice::Text))//不存在就创建，存在就打开
     {
         startSamplingBox->close();
     }
     qint64 usrlinenum=1;
     while(!user.atEnd())//检查文件是否已到达结束位置。如果返回 true，表示文件已经读取完毕
     {
         char buf[128];
         qint64 len = user.readLine(buf, sizeof(buf));

         if(len > 0)
             usrlinenum++;
         else if(len < 0)
         {
             startSamplingBox->close();
             QMessageBox::warning(this,tr("文件读取"),tr("文件读取失败！！！"));
             return;
         }
     }

//     QString usrline = QString::number(usrlinenum)  + ";" +  ui->lineEdit_name->text() + "\r\n";
     //将整数usrlinenum转换为字符串
     QString usrline = QString::number(usrlinenum)  + ";" +  ui->comboBox->currentText() + "\r\n";

     QByteArray buf = usrline.toLatin1();
     char *str = buf.data();
     user.write(str);//1;xyd
     user.close();

     /*********创建编号文件夹(存放不同人的面部信息)*********/
     QDir *pathdata = new QDir;
     bool exist = pathdata->exists(QDir::currentPath() + "/" +QString::number(usrlinenum));
     if(!exist)
     {
         bool ok =  pathdata->mkdir(QDir::currentPath() + "/" +QString::number(usrlinenum, 10));
         if(!ok)
         {
             startSamplingBox->close();
             QMessageBox::warning(this,tr("创建文件夹失败"),tr("文件夹创建失败！"));
             return;
         }
     }

     CascadeClassifier face_Classifier;   //定义人脸分类器
#if ARCH == 1
     string face_Classifier_name = "/home/xyd/tools/opencv-3.4.5/data/haarcascades/haarcascade_frontalface_alt2.xml";
#elif ARCH == 2
     string face_Classifier_name = "/usr/share/OpenCV/haarcascades/haarcascade_frontalface_alt2.xml";
#endif
     vector<Rect> face_rect;
     Mat dst_gray;
     if (!face_Classifier.load(face_Classifier_name))
     {
         startSamplingBox->close();
         QMessageBox::warning(this,"级联分类器错误","haarcascade_frontalface_alt.xml加载失败",QMessageBox::Ok);
         return;
     }

     QString pathdst = QDir::currentPath() + "/" +QString::number(usrlinenum) + "/";
     QString faceFile_path;   //./1/

     QDir dir(QDir::currentPath() + "/TEMP/");
     QStringList dst_file_name;
     dst_file_name << "*.jpg" ;
     //查找当前路径下所有的.jpg
     QFileInfoList fileList = dir.entryInfoList(dst_file_name, QDir::Files|QDir::Readable, QDir::Name);
     for(int i=0;i< fileList.size();i++)
     {
         QString photoPath = fileList[i].absoluteFilePath();//jpg绝对路径
         QString faceFile =pathdst+QString("%1.jpg").arg(i);

         Mat srcframe = imread(photoPath.toStdString());
         cvtColor(srcframe, dst_gray, cv::COLOR_BGR2GRAY);
         equalizeHist(dst_gray, dst_gray); //直方图均衡化
         //检测脸部
         face_Classifier.detectMultiScale(dst_gray,  face_rect, 1.1, 3, 0, cv::Size(90, 112));
         for (int j = 0; j < (int)face_rect.size(); j++)
         {
             Mat faceROI = srcframe(face_rect[j]);
             Mat MyFace,gray_MyFace;
             if (faceROI.cols > 100)
             {
                 cv::resize(faceROI, MyFace, cv::Size(92, 112));//92,112
                 cv::cvtColor(MyFace, gray_MyFace, CV_BGR2GRAY);
                 imwrite(faceFile.toStdString(), gray_MyFace);
             }
         }
     }
     /****************************创建csv文件******************************/
     QFile csv(QDir::currentPath() + "/csv.txt");
     if(!csv.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
     {
         startSamplingBox->close();
         QMessageBox::warning(this,"csv错误","无法创建csv文件！！！",QMessageBox::Ok);
         return;
     }

     QDir dst_file_dir(pathdst);
     QStringList dst_file_filter;
     dst_file_filter <<  "*.jpg" ;
     QFileInfoList dst_file_list_path = dst_file_dir.entryInfoList(dst_file_filter, QDir::Files | QDir::Readable, QDir::Name);

     for(int k=0; k<dst_file_list_path.size(); k++)
     {
         csv.write(QString(dst_file_list_path[k].absoluteFilePath() + ";" + QString::number(usrlinenum) + "\r\n").toLatin1());
     }
     startSamplingBox->close();
     QMessageBox::information(this,"取样结束","取样已经完成、csv文件完创建成！！！",QMessageBox::Ok);
#endif
}

void Setting::on_ButtonTrain_clicked()
{
    QMessageBox *startTrainBox = new QMessageBox(QMessageBox::Information  ,"模型训练","开始训练请稍后！！！",QMessageBox::NoButton,this);
#if FACE == 1
     startTrainBox->show();

     QFile csv(QDir::currentPath() + "/csv.txt");
     if(!csv.open(QIODevice::ReadOnly | QIODevice::Text))
     {
         startTrainBox->close();
         QMessageBox::warning(this,"csv错误","csv文件未找到！！！",QMessageBox::Ok);
         return;
     }
     vector<Mat>images;
     vector<int>labels;
     while(!csv.atEnd())
     {
         QString p = csv.readLine(128);
         QStringList qlist = p.split(";");
         if(qlist.size() == 2)
         {
             QString path =qlist[0];
             int tag = qlist[1].toInt();

             images.push_back(cv::imread(path.toStdString(), 0));
             labels.push_back(tag);
         }
     }
     if(images.size() < 1)
     {
         QMessageBox::warning(this,"训练错误","加载图片失败！！！",QMessageBox::Ok);
         return;
     }
     QString xmlfile =  QDir::currentPath() + "/facemodel.xml";
     qDebug()<<"xmlfile:"<<xmlfile;
     if(!QFile::exists(xmlfile)){
         model = LBPHFaceRecognizer::create();
         model->train(images, labels);
         qDebug()<<"train model";
     }
     /*
         第一个参数 :训练图像,用来学习的人脸图像。
         第二个参数 :人脸图像所对应的标签。
     */
     else{

         model = Algorithm::load<LBPHFaceRecognizer>(xmlfile.toStdString());
         model->update(images, labels);
         qDebug()<<"update model";
     }

     model->save("facemodel.xml");
     startTrainBox->close();
     QMessageBox::information(this,"模型训练","恭喜模型训练完成！！！",QMessageBox::Ok);
#endif
}
void Setting::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPixmap pixmap = QPixmap("://image/4.png").scaled(this->size());
    QPainter painter(this);
    painter.drawPixmap(this->rect(),pixmap);
}

//void Setting::on_lineEdit_name_textChanged(const QString &arg1)
//{
//#if FACE == 1
//     QDir *path = new QDir(QDir::currentPath() + "/TEMP");
//     bool exist = path->exists();
//     if(exist){
//         path->removeRecursively();
//         path->mkdir(QDir::currentPath() + "/TEMP");
//     }
//     count = 0;
//#endif
//}
