#ifndef DMR_COMMANDS_H
#define DMR_COMMANDS_H

enum DMRCommand {
    ChannelEnableDisable = 1,
    RCCeaseTransmission = 2,
    RCRequestCeaseTransmission = 3,
    RCPowerIncreaseOneStep = 4,
    RCPowerDecreaseOneStep = 5,
    RCMaximumPower = 6,
    RCMinimumPower = 7,
    RCNoCommand = 8
};

#endif // DMR_COMMANDS_H
