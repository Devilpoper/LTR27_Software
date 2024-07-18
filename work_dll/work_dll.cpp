#include <iostream>
#include <clocale>
#include <fstream>
#include <thread>
#include <conio.h>
#include <atomic>
#include "draw.h"
#include "../packages/lcard.ltr.ltrapi.1.32.15/build/native/include/ltr/include/ltr27api.h"
#include "../packages/IniPP.1.0.12/build/native/include/inipp.h"

#define NSAMPLES (2*LTR27_MEZZANINE_NUMBER*1024)
#define COUNT_CHANNELS 10
#define INICIALIZE_ERR 2
#define USE_MODULE_ERR 3
#define GET_CONFIG_ERR 4
#define READ_DESCR_ERR 5
#define SET_CONFIG_ERR 6
#define START_DATA_ERR 7
#define ADC_STOP_ERR   8
#define INCORRECT_FREQUENCY_DIVISIOR 10
#define ADC_NULL_RECOVER 11

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


inipp::Ini<char> getIniSettings(std::string filename) {
	inipp::Ini<char> ini;
	std::ifstream is(filename);
	
	if(!is){
		std::cout << "File may be hurt or not exist." << std::endl;
		exit(0);
	}
	
	ini.parse(is);
	return ini;
}

void showErrorsAndLog(int error) {
	std::ofstream logger;
	logger.open("process.log", std::ios::app);
	SYSTEMTIME st;
	GetLocalTime(&st);

	logger << st.wHour << ":" << st.wMinute << ":" << st.wMilliseconds << ": ";
	switch (error)
	{
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
}


//TODO вернуть проверку серийника, если тест на потоки пройдет успешно
int setupModule(TLTR27* ltr27, inipp::Ini<char> ini) {
	int res;
	const char* create_sn = ini.sections["COMMON"]["SerialNum"].c_str();

	// инициализируем поля структуры значениями по умолчанию
	res = LTR27_Init(ltr27); 
	if (res != LTR_OK) return INICIALIZE_ERR;
	
	// устанавливаем соединение с модулем находящемся в первом слоте крейта.
	res = LTR27_Open(ltr27, SADDR_DEFAULT, SPORT_DEFAULT, "", CC_MODULE1);
	if (res == LTR_WARNING_MODULE_IN_USE) return USE_MODULE_ERR;

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

double calibrationData(double avg_index_channel, int num_channel, inipp::Ini<char> ini) {
	std::string index = std::to_string(num_channel);
	double Cal_Ku = std::stof(ini.sections[index]["Cal_Ku"]);
	double Cal_0 = std::stof(ini.sections[index]["Cal_0"]);

	//std::cout << "cal_ku - " << Cal_Ku << " \tCal_0 - " << Cal_0 << std::endl;

	double Ik = Cal_Ku * (avg_index_channel - Cal_0);
	return Ik;
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

int ADCDataCollection(TLTR27* ltr27, inipp::Ini<char> ini) {
	int res = LTR_OK;
	DWORD size;
	DWORD buf[NSAMPLES];
	double avgIndexChannel[COUNT_CHANNELS];
	double minIndexChannel[COUNT_CHANNELS];
	double maxIndexChannel[COUNT_CHANNELS];
	double calibration[COUNT_CHANNELS];
	double trans[COUNT_CHANNELS];

	// Запускаем сбор данных АЦП
	res = LTR27_ADCStart(ltr27);
	if (res != LTR_OK) {
		return START_DATA_ERR;
	}

	// Создаем новый поток для проверки нажатия клавиши q
	std::thread exitWaiting(checkKeyPress);

	while (keepRunning) {
		// Забираем данные АЦП, функция возвращает количество принятых данных
		size = LTR27_Recv(ltr27, buf, NULL, NSAMPLES, 1000);

		if (size == 0) {
			return ADC_NULL_RECOVER;
		}

		if (size / 16 > 90 && size / 16 < 110) {
			double data[NSAMPLES];

			// Применяем калибровку и переводим в амперы
			// Отключаем калибровку и перевод в физ. величины, т.к. дальше делаем это вручную
			res = LTR27_ProcessData(ltr27, buf, data, &size, 1, 1);
			if (res != LTR_OK) {
				return START_DATA_ERR;
			}

			processingAvgData(data, avgIndexChannel, minIndexChannel, maxIndexChannel, size);
			
			
			for (int i = 0; i < COUNT_CHANNELS; i++) {
				calibration[i] = calibrationData(avgIndexChannel[i], i, ini);
				trans[i] = transitionData(calibration[i], i, ini);
			}

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


int main(void)
{
	setlocale(LC_ALL, "");

	int exit_code;
	TLTR27 ltr27;
	
	inipp::Ini<char> ini = getIniSettings("adc.ini"); 
	
	
	exit_code = setupModule(&ltr27, ini);

	if (exit_code == LTR_OK) {
		std::cout << "Module initialization was successful..." << std::endl;
		std::cout << "Start processing ADC module..." << std::endl;
	}
	else {
		showErrorsAndLog(exit_code);
		LTR27_Close(&ltr27);
	}
		
	
	exit_code = ADCDataCollection(&ltr27, ini);
	std::cout << "exit with code - " << exit_code << std::endl;

	if (exit_code == LTR_OK) {
		std::cout << "Module ADC worked correctly..." << std::endl;
		std::cout << "Closing the connection to the ADC module..." << std::endl;
	}
	else
		showErrorsAndLog(exit_code);

	LTR27_Close(&ltr27);
}