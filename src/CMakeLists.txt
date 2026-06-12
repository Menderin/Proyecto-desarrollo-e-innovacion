idf_component_register(
    SRCS
        "main.c"
        "i2c_bus.c"
        "tca9548a.c"
        "ir_sensor.c"
        "magnetometer.c"
        "acquisition.c"
        "classifier.c"
        "edge_impulse_adapter.cpp"
    INCLUDE_DIRS "../include"
    REQUIRES driver
)
