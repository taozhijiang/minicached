This project was deeply inspired by [memcached](https://github.com/memcached/memcached), which has been widely used in production environment for long time. It is really great for large project, and can be easly extended because the network inter-process communication method. However, I think some relative small and host-standalone application can still greatly benefit from memory-based cache method, but the network inter-process communication seems much more heavy. So I create this project, hope for the general memory-based cache libray, offering simple interface, and can be link with any linux application conviently and more efficiently.   

Without the network part, the project can be simplified without libevent, CAS data intergration, information exchange protocal and so on, pthread_mutex_t seems enough for the sync purpose.   

Well, the code can not compare with the original memcached project for the quality and elegance, but I will try my best to refine it, and more test-cases are on the way. Any advance and pull request are welcome.   




基于memcached原理修改实现的单机轻量级的缓存库   

缓存的思想、功能等将会从memcached中借鉴过来，本项目的目的是将memcached中的网络、事件等剥离开来，做成一个简单轻巧的库，方便应用程序直接编译链接，提高通信效率。   

功能正在开发完善中...   
