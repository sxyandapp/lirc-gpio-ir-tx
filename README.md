# lirc-gpio-ir-tx
raspberry pi 的lirc发送驱动  
树莓派的lirc驱动更新后，原有的lirc分割为了两部分，一个是接收驱动`gpio-ir-rx`，一个是发射驱动`gpio-ir-tx`，此外还有一个基于pwm的发射驱动`gpio-ir-tx-pwm`，这三个驱动的功能如下：  
`gpio-ir-rx`：接收来自红外接收头的数据，对原始数据解调后，转换为pulse/space数据，使用mode2等程序可直接读取驱动数据并输出pulse/space数据，此驱动激活后，会注册`/dev/lircX`设备  
`gpio-ir-tx`：将pulse/space数据通过红外发射头发射出去，默认发射的数据是使用代码实现的38khz调制  
`gpio-ir-tx-pwm`：和`gpio-ir-tx`功能类似，只不过是使用的硬件pwm实现的38khz调制，因此此驱动只能使用支持pwm的引脚，具体哪些引脚需要查阅树莓派的gpio功能说明  

---
本驱动是基于树莓派官方的`gpio-ir-tx`进行了增强，增强功能如下：  
1. 支持0hz的是数据发送，也就是发送未调制的数据，例如可以发送数据给315/433模块
2. 可以自定义`device_name`，为什么要有这个功能呢，因为当同时激活了多个`gpio-ir-tx`是，他们会注册多个设备到/dev下，名字会随机分配为/dev/lircX，且树莓派重启后此顺序会随机分配，那么如何区分每个设备呢？就是通过此`device_name`，你可以给每个设备定义不同的`device_name`，然后来判断每个/dev/lircX的功能  
  我目前的方法是在`"/sys/class/rc`路径下查看所有的lirc设备，每个设备目录里有个uevent文件，cat此文件可以看到文件内容包含了自定义的`device_name`，同时在此目录里还有个lircX，使用此名字可直接对应到/dev/lircX

> 编译及使用

1. 编译ko：确保已经下载了树莓派系统源码，然后直接运行make命令
2. 编译dtbo：直接执行makedto.sh  

或者直接使用我已经编译后的文件（我放在了installed目录里）