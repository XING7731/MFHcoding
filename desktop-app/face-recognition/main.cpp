#include "widget.h"
#include <QApplication>
#include "lldk.h"
int main(int argc, char *argv[])
{
    //qputenv("QT_IM_MODULE", QByteArray("qtvirtualkeyboard"));
    QApplication a(argc, argv);
//    Widget w;
//    w.show();
    lldk dk;
    dk.show();

    return a.exec();
}
