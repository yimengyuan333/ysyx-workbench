#include <am.h>
#include <klib-macros.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "amdev.h"

#define MAX_W 400
#define MAX_H 300

static uint32_t img_buffer[MAX_W*MAX_H];
static uint32_t colors[8] = {
  0x000000, 0xff0000, 0x00ff00, 0x0000ff,
  0xffff00, 0xff00ff, 0x00ffff, 0xffffff,
};

void draw(uint32_t color) {
  int w = io_read(AM_GPU_CONFIG).width;
  int h = io_read(AM_GPU_CONFIG).height;
  
  if (w > MAX_W) w = MAX_W;			//这里强制改变如果超出设为最大值
  if (h > MAX_H) h = MAX_H;
  int n = w * h;
  for (int i = 0; i < n; i++) {
    img_buffer[i] = color;
  }
  io_write(AM_GPU_FBDRAW, 0, 0, img_buffer, w, h, true);
}

//现在事实上draw已经完成显示一个颜色的功能了，只不过目前只会显示0x000000ff蓝，现在我们需要
//写一个新的算法函数，用来计算下一个地址，并将其运算结果作为一个uint32_t传给draw
//还得修改循环

static uint32_t cal_color_code(uint32_t c_pri, uint32_t c_next, int i, int k){
	int r0 = (c_pri >> 16) & 0xff, g0 = (c_pri >> 8) & 0xff, b0 = c_pri & 0xff;
  	int r1 = (c_next >> 16) & 0xff, g1 = (c_next >> 8) & 0xff, b1 = c_next & 0xff;
 	int r = r0 + (r1 - r0) * i / k;
 	int g = g0 + (g1 - g0) * i / k;
 	int b = b0 + (b1 - b0) * i / k;
  return (r << 16) | (g << 8) | b;//返回的是第i步的色号
}

#define STEPS          30      // 一次颜色过渡的总步数
#define FRAME_US       16667   // 正常帧间隔(微秒)，约 60 FPS
#define FRAME_FAST_US  8000    // 加速时的帧间隔，约 125 FPS
#define STEPS_PER_FRAME_FAST 2 // 加速时每帧多走几步

//改一下循环逻辑
int main() {
	ioe_init(); // initialization for GUI
	srand((unsigned)io_read(AM_TIMER_UPTIME).us);

	uint32_t cur = colors[0];
	uint32_t dst = colors[rand() % 8];
	while (dst == cur) dst = colors[rand() % 8];	// 避免起手颜色相同
	int i = 0;
	uint64_t last = io_read(AM_TIMER_UPTIME).us;		//它在循环外，初始化完就等着76行来拉它了
	bool fast = false;	// 按住任意非 ESC 键时为 true

	while (1) {
		// 一次读取键盘事件，避免 keydown 和 keycode 来自两次不同的采样
		AM_INPUT_KEYBRD_T kbd = io_read(AM_INPUT_KEYBRD);
		if (kbd.keycode != AM_KEY_NONE) {
			if (kbd.keydown) {
				if (kbd.keycode == AM_KEY_ESCAPE) break;
				fast = true;	// 按下任意其他键加速
			} else {
				fast = false;	// 松开按键恢复正常速度
			}
		}

		// now/last 都是微秒，单位一致
		uint64_t now = io_read(AM_TIMER_UPTIME).us;
		uint64_t interval = fast ? FRAME_FAST_US : FRAME_US;	//选择帧间隔
		if (now - last < interval) continue;	// 还没到下一帧，先不更新画面
		last = now;

		int advance = fast ? STEPS_PER_FRAME_FAST : 1;//选择帧输出次数
		for (int n = 0; n < advance; n++) {
			draw(cal_color_code(cur, dst, i, STEPS));
			i++;
			if (i >= STEPS) {
				cur = dst;
				dst = colors[rand() % 8];
				i = 0;
			}
		}
	}
	return 0;
}