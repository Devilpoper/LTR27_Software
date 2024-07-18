#include <iostream>

#define MIN_INDEX 3.5

void draw(double* avg, double* min, double* max, double* trans) {
	for (int i = 0; i < 10; i++) {
		if (avg[i] < MIN_INDEX) {
			std::cout << "channel " << i << ": disabled" << std::endl;
			continue;
		}

		double diff = ((max[i] - min[i]) * 100) / avg[i];
		std::cout << "channel " << i << ": " << "avg - " << avg[i] << " diff - " << diff << "%" << " trans - " << trans[i] << std::endl;
	}
}