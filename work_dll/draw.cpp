#include <iostream>

#define MIN_INDEX 3.5

void moveCursor(int x, int y) {
    std::cout << "\033[" << y << ";" << x << "H";
}

void clearScreen() {
    std::cout << "\033[2J\033[1;1H";
}

void draw(double* avg, double* min, double* max, double* trans) {
    for (int i = 0; i < 10; i++) {
        moveCursor(1, i + 1);
        if (avg[i] < MIN_INDEX) {
            std::cout << "channel " << i << ": disabled";
        }
        else {
            double diff = ((max[i] - min[i]) * 100) / avg[i];
            std::cout << "channel " << i << ": "
                << "avg - " << avg[i]
                << " diff - " << diff << "%"
                << " trans - " << trans[i];
        }
        std::cout << "                                               \n"; 
    }
    std::cout << std::flush; 
}