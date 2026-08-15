cmake_minimum_required(VERSION 3.16)
project(rmrdebuger VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 【关键修改 1】增加了 SerialPort 和 Network 模块
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Charts SerialPort Network)

qt_add_executable(rmrdebuger WIN32
    main.cpp
    MainWindow.cpp MainWindow.h
    DebugWorkers.cpp DebugWorkers.h   # 【关键修改 2】删掉JLinkWorker，换成新的双擎类
    PowerZWorker.cpp PowerZWorker.h   # Power-Z KM003C 真实电压电流计 (HID)
    datamatrix.cpp datamatrix.h         # DataMatrix ECC200 编码器 (ZXing 移植)
    functest.cpp functest.h             # 功能测试插件 (LED/光感/按键/DM码)
    app.rc
    res.qrc
)

# 【关键修改 3】在这里也加上 Qt6::SerialPort 和 Qt6::Network
target_link_libraries(rmrdebuger PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Charts Qt6::SerialPort Qt6::Network
    hid setupapi
)
