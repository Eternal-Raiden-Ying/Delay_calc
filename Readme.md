***目录清单***：  

    /dev ：可执行文件和依赖的动态库文件
    /Paser-SPEF：SPEF文件解析库
    /py ：相关python脚本文件
    /Read_train：读取netlist_info.txt文件

    /Tree: c++源代码  
    /Tree/inc: 头文件  
    /Tree/src: 函数实现  
    /Tree/resc: 机器学习导出的c代码，用作第二版优化  

    CMakeLists.txt：使用多文件结构，启用openMP，重新配置cmake规则,去掉了矩阵单元库（不依赖这个）  



***运行步骤（注意修改路径）***：  
windows:  
**不使用机器学习(默认版本)**  
course.exe --spef_num 0 --file_path "Data dir/" --feature_path "features dir/"  
**使用机器学习(v2)**  
course.exe --spef_num 0 --file_path "Data dir/" --feature_path "features dir/" --use_ml  
  
    参数说明：  
    --spef_num：spef文件的编号  
    --file_path: 输入路径(提供Data文件夹路径即可)  
        /SPEF，netlist_info.txt所在文件夹位置  
        PS：要求SPEF文件放在 输入路径/SPEF/ 下，且名称为 Group(0-1).spef；  
        要求 netlist_info.txt 在 输入路径 下  
    --feature_path：输出路径  
        输出文件所在路径，输出的延时信息文件名为 delay(0-1).txt，分别对应 Group(0-1).spef  
    --use_ml: 使用机器学习，默认不使用
    --no_ml:  显式指定不使用机器学习


**检验准确率**  
python py/compare.py --path "features dir/delay(0-1).txt" --golden "Data dir/delay_data/Group(0-1).txt"




***编译步骤***：  
linux:  

1.在当前文件夹下执行 mkdir build (如果已经存在，则无需新建)  
2.cd build  
3.cmake ..  
4.make  
5.观察到生成了可执行文件course 或者 parser_tset即可  

windows:  

使用cmake工具,如果后续更改了cmakelist.txt后请重新配置，更改代码只需要重新build（详见环境配置）  




***其他注意事项***：  
1.编译器要求支持C++17  