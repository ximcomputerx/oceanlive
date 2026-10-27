// SPDX-License-Identifier: Apache-2.0
#include "ui/IconFactory.h" // 图标工厂声明

#include <QImage> // 读取并改写程序图标
#include <QPainter> // 把图案放到正方形中央
#include <QPixmap> // 各尺寸位图

namespace { // 图标处理不对外

QImage squareIcon() { // 去掉近黑底并裁成正方形
    QImage image(QStringLiteral(":/icons/app.png")); // 资源里的程序图标
    if (image.isNull()) { // 资源缺失时不继续
        return {}; // 调用方得到空图
    } // 结束缺失判断
    image = image.convertToFormat(QImage::Format_ARGB32); // 统一成带透明通道的格式
    int minX = image.width(); // 图案左边界，先设到图外
    int minY = image.height(); // 图案上边界，先设到图外
    int maxX = -1; // 图案右边界，尚无像素
    int maxY = -1; // 图案下边界，尚无像素
    for (int y = 0; y < image.height(); ++y) { // 逐行处理
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y)); // 这一行的像素
        for (int x = 0; x < image.width(); ++x) { // 逐个像素
            const int red = qRed(line[x]); // 红色
            const int green = qGreen(line[x]); // 绿色
            const int blue = qBlue(line[x]); // 蓝色
            if (red <= 12 && green <= 12 && blue <= 12) { // 近黑视为底色
                line[x] = qRgba(red, green, blue, 0); // 底色改为透明
                continue; // 透明像素不参与裁切
            } // 结束去底
            minX = qMin(minX, x); // 收紧左边界
            minY = qMin(minY, y); // 收紧上边界
            maxX = qMax(maxX, x); // 收紧右边界
            maxY = qMax(maxY, y); // 收紧下边界
        } // 结束一行
    } // 结束整图
    if (maxX < minX || maxY < minY) { // 整张都是底色
        return {}; // 没有可显示的图案
    } // 结束空图判断
    const QImage artwork = image.copy(minX, minY, maxX - minX + 1, maxY - minY + 1); // 只留浪和太阳
    const int side = qMax(artwork.width(), artwork.height()); // 正方形边长取长边
    QImage square(side, side, QImage::Format_ARGB32); // 透明正方形
    square.fill(Qt::transparent); // 补边保持透明
    QPainter painter(&square); // 画到正方形上
    painter.drawImage((side - artwork.width()) / 2, (side - artwork.height()) / 2, artwork); // 图案居中
    painter.end(); // 结束绘制后再交给缩放
    return square; // 供各尺寸缩放
} // 结束整理图标

} // 结束匿名命名空间

QIcon IconFactory::appIcon() { // 托盘和窗口共用这一套
    const QImage source = squareIcon(); // 去底并裁成正方形的原图
    QIcon icon; // 多尺寸图标
    if (source.isNull()) { // 没有可用原图
        return icon; // 返回空图标
    } // 结束空图
    const int sizes[] = {16, 24, 32, 48, 64, 256}; // 托盘、任务栏和窗口会用到的边长
    for (int size : sizes) { // 每个边长一张
        const QPixmap pixmap = QPixmap::fromImage(source.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation)); // 平滑缩小
        icon.addPixmap(pixmap); // 放进同一套图标
    } // 结束尺寸循环
    return icon; // 系统按 DPI 选取
} // 结束取图标
