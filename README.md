# 基于IPV4的流媒体广播项目

一个使用UDP协议的流媒体音频广播项目，基于客户端/服务器模型（C/S）开发，采用UDP组播技术，实现了MP3格式的音乐广播系统。服务器采用多线程处理频道节目单和音频，解析媒体目录，读取MP3文件并通过令牌桶进行流量控制，再通过UDP组播发送。客户端采用多进程，父进程接收socket数据，输出节目单信息和进行频道选择，并将音频数据通过管道传送给子进程，子进程进行数据解码并播放。



## 设计需求

* 实现一个音乐广播系统
* 用户可以接收显示节目单，选择要听的频道
* 考虑到流量控制
* 基于客户端/服务器模型，服务器以守护进程运行



## 需求分析

* **一对多的服务**，在组播和广播之间挑选，组播和广播都比较节省资源，但是组播使用更加灵活，客户端可以根据所需数据流加入不同的组；同时，在组播中，只是只有加入了同一个组的主机可以接受到此组内的所有数据，不影响其他不需要（未加入组）的主机的通讯，所以选择**组播**；

* 首先要有一个待播放的流媒体的**文件库**，可以使用数据库实现，将流媒体文件和它们的描述文件存入数据库，启动时就连接数据库，使用时读取文件并组播出去；也可以直接**基于Linux的文件系统**来实现简易的流媒体文件库，这里采用这种方法；

* **服务器**即要发送节目单又要组播音频，可以采用**多线程**，一个节目单线程+多个频道线程，节目单线程往外发送节目单，频道线程就就读取对应目录下的流媒体文件并对外发送。并且对于用户来说，随时想要换频道了都能够打开节目单进行选择，所以节目单线程应该定期发送节目单，可以设置成1秒；

* **客户端**既要接收socket传来的流媒体数据，又要调用解码器进行播放，解码器使用mpeg123，但是不能在原进程上直接使用`exec`函数调用解码器，因为`exec`函数实际上是用新的进程镜像替换了原来的进程镜像，所以使用**父子进程**，进程间通信用**管道**。父进程负责从socket接收数据并通过管道传送给子进程，子进程调用加码器播放。

* 流媒体的数据要通过socket发送出去，就要考虑**流量控制**，主要原因在于：

  * 接收端接收能力有限，如果全部一股脑发出去的话，接收端可能接收不过来。就算接收端能承载全部数据，输给子进程之后能不能播放，父子进程用于通信的介质（管道）会不会造成文件断续；
  * UDP传输的数据报大小是有限制的，如果将一个首歌全部塞到一个包中，肯定会造成溢出，要么是传出去的数据不完整，要么就是导致当前传输失败。

  这里选择使用**令牌桶**实现流控。



## 系统框架

![image](./%E9%A1%B9%E7%9B%AE%E6%A1%86%E6%9E%B6.png)



## 本地媒体库的设计

|/var

|---media

| --- | --- ch1

| --- | --- | --- desc.text

| --- | --- | --- Bosques_de_mi_Mente.mp3

| --- | --- | --- John_Dreamer-Brotherhood.mp3

| --- | --- | --- Nightcall_Dreamerhour-Dead_V.mp3

| --- | --- ch2

| --- | --- | --- desc.text

| --- | --- | --- 理想三旬.mp3

| --- | --- | --- 安河桥.mp3

| --- | --- | --- 写给黄淮.mp3

| --- | --- | --- 春风十里.mp3

......



desc.text是当前频道的描述信息，如ch1中的desc.text中的内容为：

Pure Music:

(1) Bosques de mi Mente

(2) John Dreamer-Brotherhood

(3) Nightcall Dreamerhour-Dead V

