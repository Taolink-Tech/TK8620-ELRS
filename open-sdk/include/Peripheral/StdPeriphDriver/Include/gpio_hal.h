

#ifndef __GPIO_HAL_H__
#define __GPIO_HAL_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gpio_s gpio_module_t;

typedef enum {
    GPIO_PIN_A0 = 0, 
    GPIO_PIN_A1,     
    GPIO_PIN_A2,     
    GPIO_PIN_A3,     
    GPIO_PIN_A4,     
    GPIO_PIN_A5,     
    GPIO_PIN_A6,     
    GPIO_PIN_A7,     
    GPIO_PIN_MAX     
} gpio_pin_e;

typedef enum {
    GPIO_PULL_UP = 0, 
    GPIO_PULL_DOWN,   
    GPIO_PULL_NONE,   
    GPIO_PULL_MAX
} gpio_pull_e;

typedef enum {
    GPIO_DIR_IN = 0, 
    GPIO_DIR_OUT,    
    GPIO_DIR_MAX
} gpio_dir_e;

typedef enum iomux_e { // PMUX func
    IOMUX_GPIO           = (0),
    IOMUX_UART0_TXD      = (1),
    IOMUX_UART0_RXD      = (2),
    IOMUX_UART_RTS       = (3),
    IOMUX_UART_CTS       = (4),
    IOMUX_I2C0_SCL       = (5),
    IOMUX_I2C0_SDA       = (6),
    IOMUX_SPI0_CLK       = (7),
    IOMUX_SPI0_RXD       = (8),
    IOMUX_SPI0_TXD       = (9),
    IOMUX_SPI0_CS        = (10),
    IOMUX_DSPI_CLK       = (11),
    IOMUX_DSPI_RXD       = (12),
    IOMUX_DSPI_TXD       = (13),
    IOMUX_DSPI_CS        = (14),
    IOMUX_SIR_IN_OUT     = (15),
    IOMUX_PWM            = (16),
    IOMUX_TS             = (17),
    IOMUX_BB_TEST_BUS    = (18),
    IOMUX_JTAG_OR_CLKOUT = (19),
    IOMUX_CAP_DATA       = (20),
    IOMUX_RESVED0        = (21),
    IOMUX_EXTINT         = (22),
    IOMUX_DIG_CFG        = (23),
    IOMUX_BBU_INTR       = (24),
    IOMUX_AUTO_TRX       = (25),
    IOMUX_MAX
} gpio_af_e;

typedef enum {
    GPIO_PIN_LOW = 0U, 
    GPIO_PIN_HIGH      
} gpio_pin_state_e;

/**
 * @brief  GPIO Configuration Structure definition
 */
typedef struct {
    gpio_pin_e  pin;  
    gpio_dir_e  dir;  
    gpio_pull_e pull; 
} gpio_init_t;

void iomux_set(gpio_pin_e io_idx, gpio_af_e func);

gpio_af_e iomux_get(gpio_pin_e io_idx);

void gpio_init(gpio_module_t *gpiox, gpio_init_t *param);

void gpio_deinit(gpio_module_t *gpiox);

void gpio_pin_write(gpio_module_t *gpiox, gpio_pin_e pin, gpio_pin_state_e state);

gpio_pin_state_e gpio_pin_read(gpio_module_t *gpiox, gpio_pin_e pin);

void gpioa_init(gpio_init_t *param);

void gpioa_deinit(void);

void gpioa_pin_write(gpio_pin_e pin, gpio_pin_state_e state);

gpio_pin_state_e gpioa_pin_read(gpio_pin_e pin);

#ifdef __cplusplus
}
#endif

#endif
