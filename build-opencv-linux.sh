#!/bin/bash
set -e

SCRIPTPATH="$( cd "$(dirname "$0")" ; pwd -P )"
OPENCV_DIR=$SCRIPTPATH/opencv

if [ ! -d "$OPENCV_DIR" ]; then
    mkdir -p $OPENCV_DIR
fi
cd $OPENCV_DIR

if [ ! -d "opencv" ]; then
    git clone -b 4.4.0 https://github.com/opencv/opencv.git
fi
if [ ! -d "opencv_contrib" ]; then
    git clone -b 4.4.0 https://github.com/opencv/opencv_contrib.git
fi

mkdir -p build_opencv
cd build_opencv
cmake -DCMAKE_TOOLCHAIN_FILE=../opencv/platforms/linux/arm-gnueabi.toolchain.cmake -DOPENCV_EXTRA_MODULES_PATH=../opencv_contrib/modules  -DBUILD_LIST=photo,stitching,objdetect,tracking,imgcodecs,videoio,highgui,features2d,ml,xfeatures2d -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON ../opencv
make -j
sudo make install
