#include <iostream>
#include <clocale>
#include <fstream>
#include <thread>
#include <conio.h>
#include <atomic>
#include <windows.h>
#include "draw.h"
#include "check_ltrd.h"
#include "../packages/lcard.ltr.ltrapi.1.32.15/build/native/include/ltr/include/ltr27api.h"
#include "../packages/IniPP.1.0.12/build/native/include/inipp.h"

#include "code_errors.h"

#define NSAMPLES (2*LTR27_MEZZANINE_NUMBER*1024)
#define COUNT_CHANNELS 10


//создание глобальной переменной для циклов потоков
std::atomic<bool> keepRunning(true);

void checkKeyPress() {
	while (keepRunning) {
		if (_kbhit()) {
			char ch = _getch();
			if (ch == 'q' || ch == 'Q') {
				keepRunning = false; 
				break;
			}
		}
	}
}

void showErrorsAndLog(int error) {
	std::ofstream logger;
	logger.open("process.log", std::ios::app);
	SYSTEMTIME st;
	GetLocalTime(&st);

	logger << st.wHour << ":" << st.wMinute << ":" << st.wMilliseconds << ": ";
	switch (error)
	{
	case NOT_COMPARE_SERIAL_NUM:
		logger << "Crate has another S/N and not mathced with ini file" << std::endl;
		std::cout << "Crate has another S/N and not mathced with ini file" << std::endl;
		break;

	case ZERO_ACTIVE_CRATES:
		logger << "Crate does not connect with PC. Please reconnect crate and restart SW" << std::endl;
		std::cout << "Crate does not connect with PC. Please reconnect crate and restart SW" << std::endl;
		break;

	case LTRD_NOT_IN_RUN_MODE:
		logger << "Service LTRD not in run mode" << std::endl;
		std::cout << "Service LTRD not in run mode" << std::endl;
		break;

	case LTRD_NOT_EXIST:
		logger << "Service LTRD did not install on your PC" << std::endl;
		std::cout << "Service LTRD did not install on your PC" << std::endl;
		break;

	case SERV_CONTR_ERR:
		logger << "Failed to open Service Control Manager" << std::endl;
		std::cout << "Failed to open Service Control Manager" << std::endl;
		break;

	case FAIL_OPEN_SERV:
		logger << "Failed to open service" << std::endl;
		std::cout << "Failed to open service" << std::endl;
		break;

	case FAIL_GET_QUERRY_STAT:
		logger << "Failed to query service status" << std::endl;
		std::cout << "Failed to query service status" << std::endl;
		break;

	case INI_NOT_EXIST:
		logger << "File adc.ini may be hurt or not exist." << std::endl;
		std::cout << "File adc.ini may be hurt or not exist." << std::endl;
		break;

	case INCORRECT_SERIAL_NUM:
		logger << "Error, serial number not matched with LTR module" << std::endl;
		std::cout << "Error, serial number not matched with LTR module" << std::endl;
		break;

	case INICIALIZE_ERR:
		logger << "Error, Module will be not inicialising" << std::endl;
		std::cout << "Error, Module will be not inicialising" << std::endl;
		break;

	case USE_MODULE_ERR:
		logger << "Warning, module Already Opened" << std::endl;
		std::cout << "Warning, module Already Opened" << std::endl;
		break;

	case GET_CONFIG_ERR:
		logger << "Error getting configuration" << std::endl;
		std::cout << "Error getting configuration" << std::endl;
		break;

	case READ_DESCR_ERR:
		logger << "Error read description from module" << std::endl;
		std::cout << "Error read description from module" << std::endl;
		break;

	case SET_CONFIG_ERR:
		logger << "Error set config into module" << std::endl;
		std::cout << "Error set config into module" << std::endl;
		break;

	case ADC_STOP_ERR:
		logger << "Error ADC into module" << std::endl;
		std::cout << "Error ADC into module" << std::endl;
		break;

	case INCORRECT_FREQUENCY_DIVISIOR:
		logger << "Error: Incorrect frequency division" << std::endl;
		std::cout << "Error: Incorrect frequency division" << std::endl;
		break;

	default:
		logger << "Unknown error. Code of error - " << error << std::endl;
		std::cout << "Unknown error. Code of error - " << error << std::endl;
		break;
	}
	Sleep(1000);
}

inipp::Ini<char> getIniSettings(std::string filename) {
	inipp::Ini<char> ini;
	std::ifstream is(filename);

	if (!is) {
		showErrorsAndLog(INI_NOT_EXIST);
		exit(0);
	}

	ini.parse(is);
	return ini;
}


//TODO вернуть проверку серийника, если тест на потоки пройдет успешно
int setupModule(TLTR27* ltr27, inipp::Ini<char> ini) {
	int res;
	const char* create_sn = ini.sections["COMMON"]["SerialNum"].c_str();

	// инициализируем поля структуры значениями по умолчанию
	res = LTR27_Init(ltr27); 
	if (res != LTR_OK) return INICIALIZE_ERR;
	
	// устанавливаем соединение с первым крейтом
	res = LTR27_Open(ltr27, SADDR_DEFAULT, SPORT_DEFAULT, create_sn, CC_MODULE1);
	if (res == LTR_WARNING_MODULE_IN_USE) return USE_MODULE_ERR;
	else if (res != LTR_OK) return INCORRECT_SERIAL_NUM;

	// получаем конфигурацию модуля АЦП
	res = LTR27_GetConfig(ltr27);
	if (res != LTR_OK) return GET_CONFIG_ERR;

	// считываем описание модуля и мезонинов
	res = LTR27_GetDescription(ltr27, LTR27_ALL_DESCRIPTION);
	if (res != LTR_OK) return READ_DESCR_ERR;

	// выбираем частоту дискретизации 100Гц
	ltr27->FrequencyDivisor = 9;

	// копируем калибровочные коэффициенты
	for (int i = 0; i < LTR27_MEZZANINE_NUMBER; i++)
		for (int j = 0; j < 4; j++)
			ltr27->Mezzanine[i].CalibrCoeff[j] = ltr27->ModuleInfo.Mezzanine[i].Calibration[j];

	// передаем параметры сбора данных в модуль
	res = LTR27_SetConfig(ltr27);
	if (res != LTR_OK) return SET_CONFIG_ERR;

	return res;
}

void processingAvgData(double* data, double* avg_index_channel, double* minIndexChannel, double* maxIndexChannel, int size) {
	int counter = 1;

	//инициализируем массив ноликами
	for (int i = 0; i < COUNT_CHANNELS; i++) {
		avg_index_channel[i] = 0;
		maxIndexChannel[i] = 0;
		minIndexChannel[i] = INT_MAX;
	}
		

	//записываем все яцейки массива по четности до 10 канала
	for (int index = 0; index < size; index++)
	{
		if ((index % 16) < COUNT_CHANNELS) {
			avg_index_channel[index % 16] += data[index];
			if (minIndexChannel[index % 16] > data[index])
				minIndexChannel[index % 16] = data[index];
			if (maxIndexChannel[index % 16] < data[index])
				maxIndexChannel[index % 16] = data[index];
			counter++;
		}
	}

	counter /= 10;

	//получаем среднее значение от каналов мезонина
	for (int i = 0; i < COUNT_CHANNELS; i++) {
		avg_index_channel[i] /= counter;
	}
}

double transitionData(double Ik, int num_channel, inipp::Ini<char> ini) {
	std::string index = std::to_string(num_channel);
	double Ku = std::stof(ini.sections[index]["Ku"]);
	double Offset = std::stof(ini.sections[index]["Offset"]);

	//std::cout << "ku - " << Ku << " \toffset - " << Offset << std::endl;

	double Zn = Ku * (Ik - Offset);
	return Zn;
}

void drawData(double* avgIndexChannel) {

}

int ADCDataCollection(TLTR27* ltr27, inipp::Ini<char> ini, TLTR* unit, int reconnectFlag) {

	DWORD count_crates;

	if (reconnectFlag) {
		Sleep(500);
		LTR_GetCratesEx(unit, 0, 0, &count_crates, NULL, NULL, NULL);
		if (count_crates == 0) return RECONNECT_STATE_FUN;
	}

	int res = LTR_OK;
	int isDataCollectionStarted = 0;
	DWORD size;
	DWORD buf[NSAMPLES];
	double avgIndexChannel[COUNT_CHANNELS];
	double minIndexChannel[COUNT_CHANNELS];
	double maxIndexChannel[COUNT_CHANNELS];
	double trans[COUNT_CHANNELS];
	
	res = LTR27_ADCStart(ltr27);
	if (res != LTR_OK) return START_DATA_ERR;
	isDataCollectionStarted = 1;
	// Создаем новый поток для проверки нажатия клавиши q
	std::thread exitWaiting(checkKeyPress);

	while (keepRunning) {
		
		LTR_GetCratesEx(unit, 0, 0, &count_crates, NULL, NULL, NULL);

		if (count_crates == 0) {
			keepRunning = false;
			exitWaiting.join();
			res = LTR27_ADCStop(ltr27);
			return ADC_NULL_RECOVER;
		}

		// Забираем данные АЦП, функция возвращает количество принятых данных
		size = LTR27_Recv(ltr27, buf, NULL, NSAMPLES, 1000);

		if (size == 0) {
			return ADC_NULL_RECOVER;
		}

		if (true) {
			double data[NSAMPLES];

			// Применяем калибровку и переводим в амперы
			// Отключаем калибровку и перевод в физ. величины, т.к. дальше делаем это вручную
			res = LTR27_ProcessData(ltr27, buf, data, &size, 1, 1);
			if (res != LTR_OK) {
				return START_DATA_ERR;
			}

			processingAvgData(data, avgIndexChannel, minIndexChannel, maxIndexChannel, size);
			
			
			for (int i = 0; i < COUNT_CHANNELS; i++) 
				trans[i] = transitionData(avgIndexChannel[i], i, ini);
			

			draw(avgIndexChannel, minIndexChannel, maxIndexChannel, trans);
		}
		else {
			keepRunning = false;
			exitWaiting.join();
			res = LTR27_ADCStop(ltr27);
			return INCORRECT_FREQUENCY_DIVISIOR;
		}
	}

	std::cout << "Closing ADC process data..." << std::endl;
	exitWaiting.join();

	// Останавливаем АЦП
	res = LTR27_ADCStop(ltr27);
	if (res != LTR_OK) return ADC_STOP_ERR;
	
	return res;
}

int cmpSerialNumbers(const char* iniSerial, BYTE* crateSerial) {
	if (strlen(iniSerial) == 0) return 0;

	int i = 0;
	while (iniSerial[i] != '\0') {
		if (iniSerial[i] != crateSerial[i])
			return 0;
		i++;
	}
	return 1;
}

int main(void)
{
	setlocale(LC_ALL, "");

	std::wstring serviceName = L"ltrd";
	DWORD serviceStatus;

	int status_ltrd = IsServiceRunning(serviceName, serviceStatus);
	if (status_ltrd != 1) {
		showErrorsAndLog(status_ltrd);
	}
	else if (serviceStatus != SERVICE_RUNNING) {
		outStatus(serviceName, serviceStatus);
		showErrorsAndLog(LTRD_NOT_IN_RUN_MODE);
	}
	else {
		std::cout << "check LTRD is exist and in run mode.." << std::endl;
	}

	inipp::Ini<char> ini = getIniSettings("adc.ini");
	const char* create_sn = ini.sections["COMMON"]["SerialNum"].c_str();

	TLTR unit;
	LTR_Init(&unit);
	LTR_OpenSvcControl(&unit, LTRD_ADDR_DEFAULT, LTRD_PORT_DEFAULT);
	BYTE serialNumbers[LTR_CRATES_MAX][LTR_CRATE_SERIAL_SIZE];
	DWORD count_crates;
	
	//вызов функции для получения кол-ва подключенных крейтов
	LTR_GetCratesEx(&unit, 0, 0, &count_crates, NULL, NULL, NULL);

	//если плдключенных кретов больше 0, то берем их серийники
	if (count_crates > 0) {
		LTR_GetCrates(&unit, &serialNumbers[0][0]);
		
		if (!cmpSerialNumbers(create_sn, serialNumbers[0]))
			showErrorsAndLog(NOT_COMPARE_SERIAL_NUM);
	}
	//если подключено 0 крейтов - выводим ошибку
	else
		showErrorsAndLog(ZERO_ACTIVE_CRATES);
	

	int exit_code;
	TLTR27 ltr27;
	
	exit_code = setupModule(&ltr27, ini);

	if (exit_code == LTR_OK) {
		std::cout << "Module initialization was successful..." << std::endl;
		std::cout << "Start processing ADC module..." << std::endl;
		Sleep(1000);
		system("cls");
	}
	else {
		showErrorsAndLog(exit_code);
		LTR_Close(&unit);
		LTR27_Close(&ltr27);
		return 0;
	}
		
	exit_code = ADCDataCollection(&ltr27, ini, &unit, 0);

	while (exit_code == RECONNECT_STATE_FUN) {
		exit_code = ADCDataCollection(&ltr27, ini, &unit, 1);
	}

	if (exit_code == LTR_OK) {
		std::cout << "Module ADC worked correctly..." << std::endl;
		std::cout << "Closing the connection to the ADC module..." << std::endl;
	}
	else {
		std::cout << "exit with code - " << exit_code << std::endl;
		showErrorsAndLog(exit_code);
	}

	LTR_Close(&unit);
	LTR27_Close(&ltr27);
}