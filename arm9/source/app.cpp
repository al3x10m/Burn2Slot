#include "app.h"
#include "util.h"
#include "ID.h"

u8* sdBuffer = (u8*)malloc(sizeof(u8) * 512);
CartInfo* cart = (CartInfo*)malloc(sizeof(CartInfo) + 1);
u32 seq_1[] = { 0x555, 0x2AA, 0x555, 0x555, 0x2AA, 0x555};
u32 seq_2[] = { 0xAAA, 0x555, 0xAAA, 0xAAA, 0x555, 0xAAA};
u32 fileSize = 0;
FILE* fd;

u16 swapBits(u16 n) 
{
	u16 p1 = 0;
	u16 p2 = 1;
	u16 bit1 = (n >> p1) & 1;
	u16 bit2 = (n >> p2) & 1;
	u16 x = (bit1 ^ bit2);
	x = (x << p1) | (x << p2);
	u16 result = n ^ x;
	return result;
}

u16 read_word_rom(u32 address) 
{
	return GBAROM[address];
}
u16 read_swapped_word_rom(u32 address) 
{
	return swapBits(read_word_rom(address));
}

void write_word_rom(u32 address, u16 byte) 
{
	GBA_BUS[address] = byte;
	swiDelay(10);
}
void write_swapped_word_rom(u32 address, u16 byte) 
{
	write_word_rom(address, swapBits(byte));
}

void ( *( write_word() ) )(u32 address, u16 byte) 
{ 
	switch(cart->busType) {
		case 0: return write_word_rom; 
		case 1: return write_swapped_word_rom; 
	}
	
} 

u16 ( *( read_word() ) )(u32 address) 
{ 
	switch(cart->busType) {
		case 0: return read_word_rom; 
		case 1: return read_swapped_word_rom; 
	}
	
} 

bool waitForFlash(u32 address, bool (*isReady)(u16), int timeout){
	while(timeout && !isReady(read_word_rom(address)) ){
		swiDelay(30);
		timeout--;
	}
	if(!timeout){
		return false;
	}
	return true;
}


u32* getAddressSeq22XX() 
{
	u32* data_sec;
	switch(cart->adressSeqType) {
		case 0: data_sec = seq_1; break;
		case 1: data_sec = seq_2; break;
	}
	
	return data_sec;
}

void reset22XX() 
{
	write_word()(0, 0xF0);
}

void detect22XX() 
{
	u8 data_sec[] = { 0xAA, 0x55, 0x90};
	u16 flashid, manufactorID;
	for(cart->adressSeqType = 0; cart->adressSeqType <= 1; cart->adressSeqType++) {
		u32* addr_sec = getAddressSeq22XX();
		for(cart->busType = 0; cart->busType <= 1; cart->busType++) {
			for(u8 i = 0; i < 3; ++i) {
				write_word()(addr_sec[i], data_sec[i]);
			}
			flashid = read_word()(0x1);
			manufactorID = read_word()(0x0);
			if (((flashid >> 8) & 0xFF) == 0x22) {
				cart->flashid = flashid;
				cart->manufactorID = manufactorID;
				reset22XX();
				return;
			}
		}
	}
	cart->adressSeqType = 0
	cart->busType = 0;
}

void programmCycle22XX() 
{
	u8 data_sec[] = { 0xAA, 0x55, 0xA0};
	u32* addr_sec = getAddressSeq22XX();
	
	for(u8 i = 0; i < 3; ++i) {
		write_word()(addr_sec[i], data_sec[i]);
	}
}

void erase22XX() 
{
	u8 data_sec[] = { 0xAA, 0x55, 0x80, 0xAA, 0x55, 0x10};
	u32* addr_sec = getAddressSeq22XX();

	for(u8 i = 0; i < 6; ++i) {
		write_word()(addr_sec[i], data_sec[i]);
	}

	u16 statusReg = 0;
	while ((statusReg | 0xFF7F) != 0xFFFF) {
		statusReg = read_word()(0x0);
		swiDelay(10);
	}	
}

void resetIntel(u32 partitionSize) 
{
	for (u32 currPartition = 0; currPartition < cart->size; currPartition += partitionSize) {
		write_word()(currPartition, 0xFFFF);
	}
}

int eraseSectorIntel(int sector){
	u32 addr = sector*0x10000;

	write_word_rom(addr,0x50);
	write_word_rom(addr,0xFF);
	write_word_rom(addr,0x60);
	write_word_rom(addr,0xD0);
	write_word_rom(addr,0xFF);
	write_word_rom(addr,0x20);
	write_word_rom(addr,0xD0);
	
	waitForFlash(addr, WAIT_NON_FFFF, 0x10000);
	
	u16 statusReg = 0;
	while ((statusReg | 0xFF7F) != 0xFFFF) {
		statusReg = read_word_rom(addr);
		swiDelay(30);
	}
	
	write_word_rom(addr, 0xFF);
	return 0;
}

int writeWordIntel(u32 addr, u16 data){
	int tries = 0;
	retry:
	
	write_word_rom(addr, 0x50);
	write_word_rom(addr, 0xFF);
	
	write_word_rom(addr, 0x40);
	write_word_rom(addr, data);
	int timeout = 0x1000000;
	
	while(timeout && read_word_rom(addr) == 0xFFFF){timeout--;swiDelay(10);}
	while(timeout && read_word_rom(addr) == 0){timeout--;swiDelay(10);}
	while(timeout && !(read_word_rom(addr)&0x80)){
		timeout--;swiDelay(10);
	}
	
	//Sector was locked, unlock & erase, then try again
	if(read_word_rom(addr) & 3 && !tries){
		printTop("Retrying erase at %02X\n", addr);
		write_word_rom(addr, 0x50);
		write_word_rom(addr, 0xFF);
		write_word_rom(addr, 0x60);
		write_word_rom(addr, 0xD0);
		write_word_rom(addr, 0xFF);
		write_word_rom(addr, 0x20);
		write_word_rom(addr, 0xD0);
		tries++;
		waitForFlash(addr, WAIT_NON_FFFF, 0x10000);
		u16 statusReg = 0;
		while ((statusReg | 0xFF7F) != 0xFFFF) {
			statusReg = read_word_rom(addr);
			swiDelay(10);
		}
		goto retry;
	}

	if(!timeout || (read_word_rom(addr) & 0x10)){
		write_word_rom(addr, 0xFF);
		return -1;
	}
	
	write_word_rom(addr, 0xFF);
	
	return 0;

}

void eraseIntel()
{
	int sectorCount = 256;
	for(int i = 0; i < sectorCount; ++i) {
		eraseSectorIntel(i);
		printTop("\rERACE %d", (i + 1) * 100/sectorCount );
	}
	printTop("\n");
}

void detectIntel() {

	cart->intelType = 0;
	u16 bufSigns[] = {0x8902, 0x8904, 0x887D, 0x887E, 0x88B0};
	
	write_word_rom(0, 0xFF);
	write_word_rom(0, 0x50);
	write_word_rom(0, 0x90);
	cart->manufactorID = read_word_rom(0x0);
	cart->flashid = read_word_rom(0x2) & 0xFF00 || read_word_rom(0x4) & 0xFF;
	if (cart->flashid == 0x8802 || cart->flashid == 0x8816) {
		write_word_rom(0, 0xFF);
		cart->intelType = 3;
		return;
	}
	
	cart->flashid = read_word_rom(0x1);
	
	for(u8 i = 0; i < 4; i++){
		if(cart->flashid == bufSigns[i]) {
			cart->intelType = 2;
			write_word_rom(0, 0xFF);
			return;
		}
	}
	
	if(((cart->flashid >> 8) & 0xFF) == 0x88) {
		cart->intelType = 1;
		write_word_rom(0, 0xFF);
		return;
	}
	
	if(cart->flashid == 0xB0 && cart->manufactorID == 0xB0) {
		cart->flashid = 0x88FF;
		cart->intelType = 1;
		write_word_rom(0, 0xFF);
		return;
	}
	
	if((cart->manufactorID == 0x1C) && (cart->flashid == 0xB9)) {
		cart->intelType = 1;
		write_word_rom(0, 0xFF);
		return;
	}
	
	if((cart->manufactorID == 0x1C) && (cart->flashid == 0xB8)) {
		cart->intelType = 1;
		write_word_rom(0, 0xFF);
		return;
	}
		
	cart->intelType = 0;
	cart->flashid = 0;
}


void idFlashrom_GBA() 
{
	cart->flashid = 0;
	
	detectIntel();
	if (cart->flashid != 0) {
		return;
	}
	
	detect22XX();
	if(cart->flashid != 0) {
		return;
	}
	
	write_word_rom(0x154, 0xF0F0);
	write_word_rom(0x154, 0x9898);
	cart->flashid = read_word_rom(0x40);
	if(cart->flashid == 0x5152 && read_word_rom(0x44) == 0x5251){
		write_word_rom(0x154, 0xF0F0);
		return;
	}
	
	write_word_rom(0x0, 0xF0F0);
	write_word_rom(0xAA, 0x9898);
	cart->flashid = read_word_rom(0x20);
	if(cart->flashid == 0x5152){
		write_word_rom(0x0, 0xF0F0);
		return;
	}
	


	cart->flashid = 0;
}

void writeIntel4000_GBA() {
	for (u32 currBlock = 0; currBlock < fileSize; currBlock += 0x20000) {
		// Write to flashrom
		for (u32 currSdBuffer = 0; currSdBuffer < 0x20000; currSdBuffer += 512) {
			// Fill SD buffer
			fread(sdBuffer, 1, 512, fd);

			// Write 32 words at a time
			for (u16 currWriteBuffer = 0; currWriteBuffer < 512; currWriteBuffer += 64) {
				// Unlock Block
				write_word_rom(currBlock + currSdBuffer + currWriteBuffer, 0x60);
				write_word_rom(currBlock + currSdBuffer + currWriteBuffer, 0xD0);

				// Buffered program command
				write_word_rom(currBlock + currSdBuffer + currWriteBuffer, 0xE8);

				// Check Status register
				u16 statusReg = read_word_rom(currBlock + currSdBuffer + currWriteBuffer);
				while ((statusReg | 0xFF7F) != 0xFFFF) {
					statusReg = read_word_rom(currBlock + currSdBuffer + currWriteBuffer);
					swiDelay(10);
				}

				// Write u16 count (minus 1)
				write_word_rom(currBlock + currSdBuffer + currWriteBuffer, 0x1F);

				// Write buffer
				for (byte currByte = 0; currByte < 64; currByte += 2) {
					// Join two bytes into one word
					u16 currWord = ( ( sdBuffer[currWriteBuffer + currByte + 1] & 0xFF ) << 8 ) | ( sdBuffer[currWriteBuffer + currByte] & 0xFF );
					write_word_rom(currBlock + currSdBuffer + currWriteBuffer + currByte, currWord);
				}

				// Write Buffer to Flash
				write_word_rom(currBlock + currSdBuffer + currWriteBuffer + 62, 0xD0);

				// Read the status register at last written address
				statusReg = read_word_rom(currBlock + currSdBuffer + currWriteBuffer + 62);
				while ((statusReg | 0xFF7F) != 0xFFFF) {
					statusReg = read_word_rom(currBlock + currSdBuffer + currWriteBuffer + 62);
					swiDelay(10);
				}
			}
		}
	}
}

void write22XX() {
	u16 currWord;
	u32 address;
	for (u32 currSector = 0; currSector < fileSize; currSector += 0x20000) {
		// Write to flashrom
		for (u32 currSdBuffer = 0; currSdBuffer < 0x20000; currSdBuffer += 512) {
		// Fill SD buffer
			fread(sdBuffer, 1, 512, fd);

			// Write 32 words at a time
			for (u16 currWriteBuffer = 0; currWriteBuffer < 512; currWriteBuffer += 2) {
				// Write Buffer command
				programmCycle22XX();

				currWord = ( ( sdBuffer[currWriteBuffer + 1] & 0xFF ) << 8 ) | ( sdBuffer[currWriteBuffer] & 0xFF );
				address = (currSector + currSdBuffer + currWriteBuffer) / 2;
				write_word_rom(address, currWord);

				// Read the status register
				u16 statusReg = read_word()(address);

				while ((statusReg | 0xFF7F) != (currWord | 0xFF7F)) {
					statusReg = read_word()(address);
					swiDelay(10);
				}
			}
		}
	}
}

void writeIntel() {
	u16 currWord;
	u32 address;
	for (u32 currSector = 0; currSector < fileSize; currSector += 0x20000) {
		// Write to flashrom
		for (u32 currSdBuffer = 0; currSdBuffer < 0x20000; currSdBuffer += 512) {
		// Fill SD buffer
			fread(sdBuffer, 1, 512, fd);

			// Write 32 words at a time
			for (u16 currWriteBuffer = 0; currWriteBuffer < 512; currWriteBuffer += 2) {
				currWord = ( ( sdBuffer[currWriteBuffer + 1] & 0xFF ) << 8 ) | ( sdBuffer[currWriteBuffer] & 0xFF );
				address = (currSector + currSdBuffer + currWriteBuffer) / 2;
				writeWordIntel(address, currWord);
			}
		}
	}
}

bool flashRepro_GBA() {

	std::string filename = browseForFile({".gba"});
	fd = fopen(&filename[0], "rb");
	// Check flashrom ID's
	idFlashrom_GBA();
	u16 flashid = cart->flashid;
	u16 manufactorID = cart->manufactorID;
	
	if (flashid != 0) {
		printTop("ID: %04X Size: %DMB\n", flashid, cart->size / 0x100000);
		printTop("%s \n", getManufacturByID(manufactorID));
		u32 prev = ftell(fd);
		fseek(fd, 0L, SEEK_END);
		fileSize = ftell(fd);
		fseek(fd,prev,SEEK_SET); 
    
		// Open file on sd card
		if (fileSize) {
			// Get rom size from file
			printTop("File size: %DMB\nErasing...\n", fileSize / 0x100000);
      
			// Erase needed sectors
			if ((((flashid >> 8) & 0XFF) == 0x88) || (((flashid >> 8) & 0XFF) == 0x89) || flashid == 0xB8 || flashid == 0xB9) {
				eraseIntel();
			}
			else if (((flashid >> 8) & 0XFF) == 0x22) {
				erase22XX();
				reset22XX();
			}
			//Write flashrom
			printTop("Writing ");
			if (flashid == 0x8802 || flashid == 0x8816) {
				writeIntel4000_GBA();
			}
			else if ((((flashid >> 8) & 0XFF) == 0x88) || (((flashid >> 8) & 0XFF) == 0x89) || flashid == 0xB8 || flashid == 0xB9) {
				writeIntel();
			}
			else if (((flashid >> 8) & 0xFF) == 0x22) {
				write22XX();
			}
			
			return true;
		}
		else {
			printTop("Can't open file");
		}
	}
  	else {
		printTop("Error\n\nUnknown Flash\nFlash ID: %04X\n\n", flashid);
	}
	
	return false;
}


