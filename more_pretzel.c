void pretzel(int);

void pretzel2(int i) {
    pretzel(i - 31);
    pretzel(i - 37);
}

void pretzel3(int i) {
    pretzel(i - 41);
    pretzel(i - 43);
}

void pretzel5(int i) {
    pretzel(i - 47);
    pretzel(i - 53);
}

void pretzel(int i) {
    if (i < 0) return;
    if (i % 2 == 0) {
        pretzel2(i / 2);
    }
    if (i % 3 == 0) {
        pretzel3(i / 3);
    }
    if (i % 5 == 0) {
        pretzel5(i / 5);
    }
}
