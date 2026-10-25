#include "sensor.h"

void local_temp_humdity_task(void *arg)
{
    i2c_master_bus_config_t i2c_config = {
        .i2c_port = -1, .sda_io_num = GPIO_NUM_47, .scl_io_num = GPIO_NUM_48, .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags = {.enable_internal_pullup = true}};
    i2c_master_bus_handle_t i2c_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_config, &i2c_handle));
    i2c_master_dev_handle_t shtc3_handle = shtc3_device_create(i2c_handle, SHTC3_I2C_ADDR, 400000);

    float t = 0.0f, h = 0.0f;
    while (1)
    {
        if (shtc3_get_th(shtc3_handle, SHTC3_REG_T_CSE_NM, &t, &h) == ESP_OK)
        {
            float calibrated_t = t - 5.9;
            float clibrated_h = h + 17; 
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            g_env_temp = calibrated_t;
            g_env_hum = clibrated_h;
            g_shtc3_ok = true;
            xSemaphoreGive(data_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}