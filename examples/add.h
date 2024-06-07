//
// Created by Carter Mbotho on 2024-04-26.
//

struct Create {
    int id;
    double value;
};

struct Delete {
    int id;
    double value;
};

union Command {
    struct Create create;
    struct Delete del;
};

void sendCommand(union Command *command);
