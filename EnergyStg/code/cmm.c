#include "apply.h"
#include "cmm.h"
#include "pu.h"

extern CMMPARA  cmm_w[CMM_READ_N];
extern CMMPARA  cmm_r[CMM_WRITE_N];

CMM cmm = CMM_DEFAULTS;
unsigned cmm_delay = 0;

#define		F2U(a)		((a>0)?	(unsigned)(a) :	(unsigned)(int)(a))		//a= [ -32768  65535]
void cmm_read_reset1(void *p);

// 描述：初始化
void cmm_init(void)
{
	unsigned i;
	float tmp;
	unsigned data,data2;

	// R表初始化
   	for (i=0; i<CMM_READ_N;	i++){
   		if      (cmm_r[i].ptype=='U')	*(unsigned *)cmm_r[i].p = (unsigned)(cmm_r[i].factory / cmm_r[i].pu);
   		else if (cmm_r[i].ptype=='I')	*(int *     )cmm_r[i].p = (int     )(cmm_r[i].factory / cmm_r[i].pu);
   		else							*(float *   )cmm_r[i].p = (float   )(cmm_r[i].factory / cmm_r[i].pu);

   		//对屏幕的设置参数做初始化 R表
		tmp = cmm_r[i].factory/cmm_r[i].resolution;
   		drv_scib_write(cmm_r[i].addr, F2U(tmp));
		DELAY_US(20000);							//做了个死等延时，
	}

	// W表初始化
   	for (i=0; i<CMM_WRITE_N;	i++){
   		if      (cmm_w[i].ptype=='U')	*(unsigned *)cmm_w[i].p = (unsigned)(cmm_w[i].factory / cmm_w[i].pu);
   		else if (cmm_w[i].ptype=='I')	*(int *     )cmm_w[i].p = (int     )(cmm_w[i].factory / cmm_w[i].pu);
   		else							*(float *   )cmm_w[i].p = (float   )(cmm_w[i].factory / cmm_w[i].pu);
	}

	cmm.dis_on = 1;

	cmm.di_local = 1;							// 默认是触摸屏控制   0:主控控制 remote 1:触摸屏控制 Local
}


// 描述：仲裁输入参数，计算只在此模块内用到的输出参数
void cmm_arb(void)
{
	if(cmm.bclear_error && !PcsA_1.BatRunState)	    except_clear_error();	// 故障复位

    charge_speedA();    // 降电流的时间可设置更新

}


//表读
unsigned  cmm_data2real( unsigned rw_table, unsigned bwork, unsigned i, unsigned data,  float *preal)
{
	float tmp, tmp2;

	if(!rw_table) {			// rw_table=0:处理配置数据
		if(i >= CMM_READ_N)		return 0;
		
		if      (cmm_r[i].ptype=='U')	tmp = (unsigned)data;
		else if (cmm_r[i].ptype=='I')	tmp = (int     )data;
		else							tmp = (int     )data;
				
		tmp2 = tmp * cmm_r[i].resolution;
			
		if (!(bwork && cmm_r[i].rsw=='S') && tmp2>=cmm_r[i].low && tmp2<=cmm_r[i].upper){
			*preal = tmp2 / cmm_r[i].pu;
			return 1;
		}
		return 0;
		
	}else{			// rw_table=1:处理运行数据

		if(i >= CMM_WRITE_N)	return 0;
			
		if      (cmm_w[i].ptype=='U')	tmp = (unsigned)data;
		else if (cmm_w[i].ptype=='I')	tmp = (int     )data;
		else							tmp = (int     )data;
			
		tmp2 = tmp * cmm_w[i].resolution;

		if (tmp2>=cmm_w[i].low && tmp2<=cmm_w[i].upper){
			*preal = tmp2 / cmm_w[i].pu;
			return 1;
		}
		return 0;
	}
}


//表写
//void cmm_real2data(unsigned rw_table, unsigned id,  unsigned i, unsigned *pdata)
void cmm_real2data(unsigned rw_table, unsigned i, unsigned offset, unsigned *pdata)
{
	float tmp, tmp2;
	
	if(!rw_table) {				// rw_table=0:处理配置数据
		if(i >= CMM_READ_N)		return;
		
		if      (cmm_r[i].ptype=='U')	tmp = *(unsigned *   )cmm_r[i].p;
		else if (cmm_r[i].ptype=='I')	tmp = *(int *  		 )cmm_r[i].p;
		else							tmp = *(float *    	 )cmm_r[i].p;
		
		tmp2 = tmp  *  cmm_r[i].pu   /  cmm_r[i].resolution;
		*pdata = F2U(tmp2);		
	}else{						// rw_table=1:处理运行数据

		if(i >= CMM_WRITE_N)	return;

		if      (cmm_w[i].ptype=='U')	tmp = *((unsigned * )cmm_w[i].p + offset);
		else if (cmm_w[i].ptype=='I')	tmp = *((int *  	)cmm_w[i].p + offset);
		else							tmp = *((float *    )cmm_w[i].p + offset);
		
		tmp2 = tmp  *  cmm_w[i].pu   /  cmm_w[i].resolution;
		*pdata = F2U(tmp2);		 
	}
}


void cmm_real_handle(unsigned rw_table, unsigned id, unsigned i, float real)
{
	if(!rw_table) {			// rw_table=0:处理配置数据
		if      (cmm_r[i].ptype=='U')	*(unsigned *)cmm_r[i].p = (unsigned)real;
		else if (cmm_r[i].ptype=='I')	*(int *     )cmm_r[i].p = (int     )real;
		else							*(float *   )cmm_r[i].p = (float   )real;
	}else if(rw_table){		// rw_table=1:处理运行数据
		if      (cmm_w[i].ptype=='U')	*(unsigned *)cmm_w[i].p = (unsigned)real;
		else if (cmm_w[i].ptype=='I')	*(int *     )cmm_w[i].p = (int     )real;
		else							*(float *   )cmm_w[i].p = (float   )real;
	}
}


//面板-->主DSP	R表
void cmm_read_pnl(unsigned bwork)
{
	static unsigned i=0;
	unsigned data;
	float real;
	static unsigned di_local_old = 1;
	static unsigned cnt_cmm_err = 0;							// 屏幕通信故障计数器

#define CMM_IN_START	6										// 如果是远程控制,屏上留了几个参数可以设置和显示
#define CMM_IN_END		10

	if(!cmm.dis_on)										return;	// 是主机才运行下面的程序,去读屏

	if (cmm.di_local != di_local_old){							// 近/远程控制切换
		if (cmm.di_local)	i = 0;	else	i = CMM_IN_START;	// 0:主控控制 remote 1:触摸屏控制 Local
		di_local_old = cmm.di_local;
	}

	//来自面板，参数赋值
	if (!drv_scib_read(cmm_r[i].addr, &data)){
		if (cnt_cmm_err < 10){
			cnt_cmm_err++;	
		}else{
//bltest			EXCEPT_JUDGE_SET(1, ERROR_COMM);					// 屏幕通信故障
			cnt_cmm_err = 0;
		}
		return;
	}

	if (cmm_data2real(CMM_RW_TABLE_R, bwork, i, data,  &real))	cmm_real_handle(CMM_RW_TABLE_R, 0, i, real);

	if (i < (CMM_READ_N-1))	i++;	else 	i=0;			// 触摸屏控制，地址从0到CMM_READ_N-1
}



// 主DSP --> 面板	W表
void cmm_write_pnl(void)
{
	static unsigned i=0;
	unsigned data;

	if (!cmm.dis_on)								return;		// 不是主机 且 没有屏 返回

	cmm_real2data(CMM_RW_TABLE_W, i, 0,	&data);					// rw_table=1:处理运行数据

	drv_scib_write(cmm_w[i].addr, data);

	if (i < (CMM_WRITE_N-1))		i++;	else 	i=0;
}


// 描述：回写参数
// 作者：王潞钢
// 版本：4.0
// 日期：星期三,2013-6-19
void cmm_reset_parameter(void)
{
	static CLK clk_clear = CLK_DEFAULTS;
	static unsigned turn_flag = 0;
	unsigned index = 0;
	static unsigned reset_index = 0;
	static unsigned array[5] = {0,0,0,0,0};

	if (!turn_flag) {

		if ((except.error)&&(cmm.bready)) 					array[index++] = 1;  // 回写bready
		if ((except.error)&&(cmm.bstart_chg)) 	 			array[index++] = 2;  // 回写bstart_lsc
		if ((except.error)&&(cmm.bstart_dchg))	 			array[index++] = 3;  // 回写bstart_ssc
		if (clk_Bjudge1(&clk_clear, cmm.bclear_error, 2000L)) {
			except_clear_error();							array[index++] = 4;} // 回写bclear_error
	//	if ( ) array[index++] = ; // 回写
		turn_flag = 1;
	}
	if (array[reset_index]) {
		switch (array[reset_index]) {
			case 1:		cmm_read_reset1((void *)(&cmm.bready));			array[reset_index++] = 0;	break;    // 回写bready            
			case 2:		cmm_read_reset1((void *)(&cmm.bstart_chg));		array[reset_index++] = 0;	break;    // 回写bstart_LSC
			case 3:		cmm_read_reset1((void *)(&cmm.bstart_dchg));	array[reset_index++] = 0;	break;    // 回写bstart_GSC
			case 4:		cmm_read_reset1((void *)(&cmm.bclear_error));	array[reset_index++] = 0;	break;    // 回写bclear_error      
			//case  :	cmm_read_reset1( );								array[reset_index++] = 0;	break;    // 回?                  
			default:	break;
		}
	}else{ // 本轮结束
		reset_index = 0;
		turn_flag = 0;
	}
}


// 描述：对某读取的参数，复位成工厂值。cmm_read_reset((void *)cmm.bready);
// 作者：王潞钢
// 版本：4.0
// 日期：星期三,2013-6-19
void cmm_read_reset1(void *p)
{
	unsigned i;
	float tmp;
	
	for (i=0; i<CMM_READ_N;	i++){
   		if (p == cmm_r[i].p){
   			
   			if      (cmm_r[i].ptype=='U')	*(unsigned *)cmm_r[i].p = (unsigned)(cmm_r[i].factory / cmm_r[i].pu);
	   		else if (cmm_r[i].ptype=='I')	*(int *     )cmm_r[i].p = (int     )(cmm_r[i].factory / cmm_r[i].pu);
   			else							*(float *   )cmm_r[i].p = (float   )(cmm_r[i].factory / cmm_r[i].pu);

			tmp = cmm_r[i].factory / cmm_r[i].resolution;
   			drv_scib_write(cmm_r[i].addr, F2U(tmp));

			return;		//break;
		}
	}
}
		
void cmm_lemp_fan(void)
{
	static CLK clk0 = CLK_DEFAULTS;

	if (!clk_Bjudgen(&clk0, 1, 100L)) 	return;			// 1s执行一次的初始化

	if (fsm_chg_bwork){
		drv_cpld_do_yellow(1);		// 充电指示灯
	}else{
		drv_cpld_do_yellow(0);
	}
	if (fsm_dchg_bwork){
		drv_cpld_do_blue(1);		// 放电指示灯
		drv_cpld_do_red(0);         // 不是放电则一定桥月�
	}else{
		drv_cpld_do_blue(0);		// 
		drv_cpld_do_red(1);
	}
}
