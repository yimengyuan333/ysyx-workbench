# "一生一芯"工程项目

这是"一生一芯"的工程项目. 通过运行
```bash
bash init.sh subproject-name
```
进行初始化, 具体请参考[实验讲义][lecture note].

[lecture note]: https://ysyx.oscc.cc/docs/

推送命令：am-kernels和主仓库都要提交
cd am-kernels && git add -A && git commit -m "..." && git push
cd ..          && git add am-kernels && git commit -m "..." && git push