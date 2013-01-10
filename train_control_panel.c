#include <plio.h>
// #include <bwio.h>
#include <ts7200.h>
#include <debug.h>

#define ZERO 0x00000000
#define ONE 0xffffffff

#define ASCI_ESC 27
#define ASCI_CLEAR_SCREEN "2J"
#define ASCI_CLEAR_TO_EOL "K"
#define ASCI_CURSOR_SAVE "s"
#define ASCI_CURSOR_RETURN "u"
#define ASCI_CURSOR_TO "H"
#define ASCI_BACKSPACE '\b'

#define LINE_ELAPSED_TIME "1"
#define LINE_LAST_COMMAND "2"
#define LINE_USER_INPUT "30"
#define LINE_DEBUG "32"
#define LINE_BOTTOM "35"

#define COLUMN_FIRST "1"

static unsigned int dbflags;

/* 
 * Timer Methods
 */

unsigned int setTimerControl(int timer_base, unsigned int enable, unsigned int mode, unsigned int clksel) {
	unsigned int* timer_control_addr = (unsigned int*) (timer_base + CRTL_OFFSET);
	DEBUG(DB_TIMER, "Timer3 base: 0x%x ctrl addr: 0x%x offset: 0x%x.\n", timer_base, timer_control_addr, CRTL_OFFSET);
	
	unsigned int control_value = (ENABLE_MASK & enable) | (MODE_MASK & mode) | (CLKSEL_MASK & clksel) ;
	DEBUG(DB_TIMER, "Timer3 control changing from 0x%x to 0x%x.\n", *timer_control_addr, control_value);
	
	*timer_control_addr = control_value;
	return *timer_control_addr;
}

unsigned int getTimerValue(int timer_base) {
	unsigned int* timer_value_addr = (unsigned int*) (timer_base + VAL_OFFSET);
	unsigned int value = *timer_value_addr;
	return value;
}

/* 
 * IO Methods
 */

void printAsciControl(int channel, char *control, char *arg1, char *arg2) {
	plprintf(channel, "%c[", ASCI_ESC);
	if(arg1) plprintf(channel, "%s", arg1);
	if(arg2) plprintf(channel, ";%s", arg2);
	plprintf(channel, "%s", control);
}

void setRegisterBit(int base, int offset, int mask, int value) {
	int *addr = (int *)(base + offset);
	int buf = *addr;
	buf = value ? buf | mask : buf & ~mask;
	*addr = buf;
	return;
}

int getRegister(int base, int offset) {
	int *addr = (int *)(base + offset);
	return *addr;
}

int getRegisterBit(int base, int offset, int mask) {
	int *addr = (int *)(base + offset);
	return (*addr) & mask;
}

/* 
 * Main Polling Loop
 */

int main(int argc, char* argv[]) {
	
	/* Initialize Global Variables */
	char plio_buffer[CHANNEL_COUNT * OUTPUT_BUFFER_SIZE];
	unsigned int plio_send_index[CHANNEL_COUNT];
	unsigned int plio_save_index[CHANNEL_COUNT];
	char i2a_buffer[12];
	dbflags = DB_USER_INPUT | DB_TRAIN_CTRL; // Debug Flags
	
	/* Initialize IO: setup buffer; BOTH: turn off fifo; COM1: speed to 2400, enable stp2 */
	plbootstrap(plio_buffer, plio_send_index, plio_save_index);
	plsetfifo(COM2, OFF);
	plsetfifo(COM1, OFF);
	plsetspeed(COM1, 2400);
	setRegisterBit(UART1_BASE, UART_LCRH_OFFSET, STP2_MASK, ONE);
	
	printAsciControl(COM2, ASCI_CLEAR_SCREEN, 0, 0);
	
	/* Verifiying COM1's Configuration: nothing when debug flag is turned off */
	printAsciControl(COM2, ASCI_CURSOR_TO, LINE_DEBUG, COLUMN_FIRST);
	DEBUG(DB_IO, "COM1 LCRH: 0x%x\n", getRegister(UART1_BASE, UART_LCRH_OFFSET)); // 0x68
	DEBUG(DB_IO, "COM1 LCRM: 0x%x\n", getRegister(UART1_BASE, UART_LCRM_OFFSET)); // 0x0
	DEBUG(DB_IO, "COM1 LCRL: 0x%x\n", getRegister(UART1_BASE, UART_LCRL_OFFSET)); // 0xbf
	DEBUG(DB_IO, "COM1 CTRL: 0x%x\n", getRegister(UART1_BASE, UART_CTLR_OFFSET)); // 0x1
	DEBUG(DB_IO, "COM1 FLAG: 0x%x\n", getRegister(UART1_BASE, UART_FLAG_OFFSET)); // 0x91
	DEBUG(DB_IO, "IO Initialized.\n");
	
	/* Initialize Timer: Enable Timer3 with free running mode and 2kHz clock */
	unsigned int previous_timer_value = 0;
	unsigned int timer_tick_remained = 0;
	unsigned int tenth_sec_elapsed = 0;
	
	setTimerControl(TIMER3_BASE, ONE, ZERO, ZERO);
	previous_timer_value = getTimerValue(TIMER3_BASE);
	DEBUG(DB_TIMER, "Timer3 value start with 0x%x.\n", previous_timer_value);
	
	/* Initialize User Input Buffer */
	char user_input_buffer[1000];
	unsigned int user_input_size = 0;
	char user_input_char;
	user_input_buffer[user_input_size] = '\0';
		
	/* Polling loop */
	while(1) {
		
		/* Polling IO: Give it a chance to send out char */
		plsend(COM1);
		plsend(COM2);
		
		/* Timer: Calculate and display time elapsed */
		unsigned int timer_value = getTimerValue(TIMER3_BASE);
		unsigned int time_elapsed = previous_timer_value - timer_value;
		
		// Fix time_elapsed when underflow
		if(timer_value > previous_timer_value) {
			time_elapsed = previous_timer_value + (ONE - timer_value);
		}
		
		// If time elapsed more than 1/10 sec
		if(time_elapsed >= 200)
		{
			// Add elapsed time into remaining ticks, then convert to tenth-sec
			timer_tick_remained += time_elapsed;
			for(;timer_tick_remained >= 200; timer_tick_remained -= 200) tenth_sec_elapsed++;
			
			printAsciControl(COM2, ASCI_CURSOR_TO, LINE_ELAPSED_TIME, COLUMN_FIRST);
			plprintf(COM2, "Time elapsed: %d:%d,%d, timer value: 0x%x\n", tenth_sec_elapsed / 600, (tenth_sec_elapsed % 600) / 10, tenth_sec_elapsed % 10, timer_value);
			plui2a(user_input_size + 1, 10, i2a_buffer);
			printAsciControl(COM2, ASCI_CURSOR_TO, LINE_USER_INPUT, i2a_buffer);
			previous_timer_value = timer_value;
		}
		
		/* User Input */
		if(plgetc(COM2, &user_input_char) > 0 && user_input_size < 1000) {
			
			// Push or pop char from user_input_buffer
			if(user_input_char == ASCI_BACKSPACE) {
				user_input_size--;
				user_input_buffer[user_input_size] = '\0';
			}
			else {
				user_input_buffer[user_input_size] = user_input_char;
				user_input_size++;
				user_input_buffer[user_input_size] = '\0';
			}
			
			// If is EOL
			if(user_input_char == '\n' || user_input_char == '\r') {
				DEBUG(DB_USER_INPUT, "User Input: Reach EOL. Input Size %u, value %s", user_input_size, user_input_buffer);
				// If is q, quit
				if(user_input_size == 2 && user_input_buffer[0] == 'q') {
					break;
				}
				
				// If is r, start the train
				if(user_input_size == 2 && user_input_buffer[0] == 'r') {
					DEBUG(DB_TRAIN_CTRL, "Sending start\n");
					plputc(COM1, 96);
				}
				
				// If is s, stop the train
				if(user_input_size == 2 && user_input_buffer[0] == 's') {
					DEBUG(DB_TRAIN_CTRL, "Sending stop\n");
					plputc(COM1, 97);
				}
				
				// Send to last command
				printAsciControl(COM2, ASCI_CURSOR_TO, LINE_LAST_COMMAND, COLUMN_FIRST);
				printAsciControl(COM2, ASCI_CLEAR_TO_EOL, 0, 0);
				plputstr(COM2, user_input_buffer);
				
				// Reset input buffer
				user_input_buffer[0] = '\0';
				user_input_size = 0;
			}
			
			// Refresh Input Display
			printAsciControl(COM2, ASCI_CURSOR_TO, LINE_USER_INPUT, COLUMN_FIRST);
			printAsciControl(COM2, ASCI_CLEAR_TO_EOL, 0, 0);
			plputstr(COM2, user_input_buffer);
		 }
	}
	
	printAsciControl(COM2, ASCI_CURSOR_TO, LINE_BOTTOM, COLUMN_FIRST);
	
	plflush(COM1);
	plflush(COM2);
	
	plstat();
	return 0;
}
