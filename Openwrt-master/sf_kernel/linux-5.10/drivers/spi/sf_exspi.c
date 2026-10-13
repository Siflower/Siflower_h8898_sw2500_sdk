#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/sh_clk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

#include <linux/spi/spi-mem.h>
#include <linux/spi/spi.h>

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>

#define SPI_XFER_BEGIN		BIT(0)
#define SPI_XFER_END		BIT(1)

/*
 * siflower SSP fifo level
 * */
#define SF_SSP_FIFO_LEVEL		0x100
/*
 * siflower SSP register
 * */
#define SSP_CR0				0x000
#define SSP_CR1				0x004
#define SSP_DR				0x008
#define SSP_SR				0x00C
#define SSP_CPSR			0x010
#define SSP_IMSC			0x014
#define SSP_RIS				0x018
#define SSP_MIS				0x01C
#define SSP_ICR				0x020
#define SSP_DMACR			0x024
#define SSP_FIFO_LEVEL		0x028
#define SSP_EXSPI_CMD0		0x02C
#define SSP_EXSPI_CMD1		0x030
#define SSP_EXSPI_CMD2		0x034
/* SSP Control Register 0  - SSP_CR0 */
#define SSP_CR0_EXSPI_FRAME (0x3 << 4)
#define SSP_CR0_SPO			(0x1 << 6)
#define SSP_CR0_SPH			(0x1 << 7)
#define SSP_CR0_BIT_MODE(x) ((x)-1)
#define SSP_SCR_MIN			(0x00)
#define SSP_SCR_MAX			(0xFF)
#define SSP_SCR_SHFT		8
#define DFLT_CLKRATE		2
/* SSP Control Register 1  - SSP_CR1 */
#define SSP_CR1_MASK_SSE	(0x1 << 1)
#define SSP_CPSR_MIN		(0x02)
#define SSP_CPSR_MAX		(0xFE)
#define DFLT_PRESCALE		(0x40)
/* SSP Status Register - SSP_SR */
#define SSP_SR_MASK_TFE		(0x1 << 0) /* Transmit FIFO empty */
#define SSP_SR_MASK_TNF		(0x1 << 1) /* Transmit FIFO not full */
#define SSP_SR_MASK_RNE		(0x1 << 2) /* Receive FIFO not empty */
#define SSP_SR_MASK_RFF		(0x1 << 3) /* Receive FIFO full */
#define SSP_SR_MASK_BSY		(0x1 << 4) /* Busy Flag */

/* SSP FIFO Threshold Register - SSP_FIFO_LEVEL */
#define SSP_FIFO_LEVEL_RX	GENMASK(14, 8) /* Receive FIFO watermark */
#define SSP_FIFO_LEVEL_TX	GENMASK(6, 0) /* Transmit FIFO watermark */
#define DFLT_THRESH_RX		32
#define DFLT_THRESH_TX		32

/* SSP Raw Interrupt Status Register - SSP_RIS */
#define SSP_RIS_MASK_RORRIS	(0x1 << 0) /* Receive Overrun */
#define SSP_RIS_MASK_RTRIS	(0x1 << 1) /* Receive Timeout */
#define SSP_RIS_MASK_RXRIS	(0x1 << 2) /* Receive FIFO Raw Interrupt status */
#define SSP_RIS_MASK_TXRIS	(0x1 << 3) /* Transmit FIFO Raw Interrupt status */

/* EXSPI command register 0 SSP_EXSPI_CMD0 */
#define EXSPI_CMD0_CMD_COUNT	BIT(0)		/* cmd byte, must be set at last */
#define EXSPI_CMD0_ADDR_COUNT	GENMASK(2, 1)	/* addr bytes */
#define EXSPI_CMD0_EHC_COUNT	BIT(3)		/* Set 1 for 4-byte address mode */
#define EXSPI_CMD0_TX_COUNT	GENMASK(14, 4)	/* TX data bytes */
#define EXSPI_CMD0_VALID	BIT(15)		/* Set 1 to make the cmd to be run */

/* EXSPI command register 1 SSP_EXSPI_CMD1 */
#define EXSPI_CMD1_DUMMY_COUNT	GENMASK(3, 0)	/* dummy bytes */
#define EXSPI_CMD1_RX_COUNT	GENMASK(14, 4)	/* RX data bytes */

/* EXSPI command register 2 SSP_EXSPI_CMD2 */
/* Set 1 for 1-wire, 2 for 2-wire, 3 for 4-wire */
#define EXSPI_CMD2_CMD_IO_MODE	GENMASK(1, 0)	/* cmd IO mode */
#define EXSPI_CMD2_ADDR_IO_MODE	GENMASK(3, 2)	/* addr IO mode */
#define EXSPI_CMD2_DATA_IO_MODE	GENMASK(5, 4)	/* data IO mode */

#define SF_READ_TIMEOUT		(10 * HZ)
#define MAX_S_BUF			100

struct sf_exspi {
	void __iomem *base;
	struct clk *clk, *apbclk;
	struct device *dev;
	struct mutex lock;
	int freq;		/* DT initial speed, applied by default_setup */
	u32 cur_freq;		/* speed currently programmed */
	u16 cur_cpsr, cur_scr;	/* divider pair matching cur_freq */
	struct gpio_descs *cs_gpio;
};

static void spi_cs_set_activate(struct sf_exspi *s, u8 cs, bool on_off)
{
	if(s->cs_gpio)
		gpiod_set_value_cansleep(s->cs_gpio->desc[cs], on_off ? 1 : 0);
}

static irqreturn_t sf_exspi_irq_handler(int irq, void *dev_id) {
	/*struct sf_exspi *s = dev_id;
	u32 reg;*/

	/* clear interrupt */
	/*reg = ioread32(s->base + QUADSPI_FR);
	iowrite32(q, reg, q->iobase + QUADSPI_FR);

	if (reg & QUADSPI_FR_TFF_MASK)
		complete(&q->c);

	dev_dbg(q->dev, "QUADSPI_FR : 0x%.8x:0x%.8x\n", 0, reg);
	return IRQ_HANDLED;*/
	return 0;
}

static void sf_exspi_flush_rxfifo(struct sf_exspi *s) {
	while (readw(s->base + SSP_SR) & SSP_SR_MASK_RNE)
		readw(s->base + SSP_DR);
}

static int sf_exspi_wait_not_busy(struct sf_exspi *s) {
	unsigned long timeout = jiffies + SF_READ_TIMEOUT;

	do {
		if (!(readw(s->base + SSP_SR) & SSP_SR_MASK_BSY))
			return 0;

		cond_resched();
	} while (time_after(timeout, jiffies));

	dev_err(s->dev, "I/O timed out\n");
	return -ETIMEDOUT;
}

static int sf_exspi_wait_rx_not_empty(struct sf_exspi *s) {
	unsigned long timeout = jiffies + SF_READ_TIMEOUT;

	do {
		if (readw(s->base + SSP_SR) & SSP_SR_MASK_RNE)
			return 0;

		cond_resched();
	} while (time_after(timeout, jiffies));

	dev_err(s->dev, "read timed out\n");
	return -ETIMEDOUT;
}

static int sf_exspi_wait_txfifo_not_full(struct sf_exspi *s) {
	unsigned long timeout = jiffies + SF_READ_TIMEOUT;

	do {
		if (readw(s->base + SSP_SR) & SSP_SR_MASK_TNF)
			return 0;

		cond_resched();
	} while (time_after(timeout, jiffies));

	dev_err(s->dev, "write timed out\n");
	return -ETIMEDOUT;
}

static int sf_exspi_wait_rxfifo(struct sf_exspi *s) {
	unsigned long timeout = jiffies + SF_READ_TIMEOUT;

	do {
		if (readw(s->base + SSP_RIS) & SSP_RIS_MASK_RXRIS)
			return 0;

		cond_resched();
	} while (time_after(timeout, jiffies));

	dev_err(s->dev, "read timed out\n");
	return -ETIMEDOUT;
}

static void sf_exspi_enable(struct sf_exspi *s) {
	/* Enable the SPI hardware */
	writew(SSP_CR1_MASK_SSE, s->base + SSP_CR1);
}

static void sf_exspi_disable(struct sf_exspi *s) {
	/* Disable the SPI hardware */
	writew(0, s->base + SSP_CR1);
}

static void sf_exspi_xmit(struct sf_exspi *s, unsigned int nbytes, const u8 *out)
{
	while (nbytes--)
		writew(*out++, s->base + SSP_DR);
}

static int sf_exspi_rcv(struct sf_exspi *s, unsigned int nbytes, u8 *in)
{
	int ret, i;

	while (nbytes >= DFLT_THRESH_RX) {
		/* wait for RX FIFO to reach the threshold */
		ret = sf_exspi_wait_rxfifo(s);
		if (ret)
			return ret;

		for (i = 0; i < DFLT_THRESH_RX; i++)
			*in++ = readw(s->base + SSP_DR);

		nbytes -= DFLT_THRESH_RX;
	}

	/* read the remaining data */
	while (nbytes) {
		ret = sf_exspi_wait_rx_not_empty(s);
		if (ret)
			return ret;

		*in++ = readw(s->base + SSP_DR);
		nbytes--;
	}

	return 0;
}

static inline u32 spi_rate(u32 rate, u16 csdvsr, u16 scr) {
	return rate / (csdvsr * (1 + scr));
}

static void sf_exspi_calc_div(struct sf_exspi *s, u32 speed, u16 *cpsr, u16 *scr) {
	u32 rate = clk_get_rate(s->clk);
	u32 min, max, best = 0, tmp;
	u16 c, sc, best_cpsr = SSP_CPSR_MAX, best_scr = SSP_SCR_MAX;
	bool found = false;

	max = spi_rate(rate, SSP_CPSR_MIN, SSP_SCR_MIN);
	min = spi_rate(rate, SSP_CPSR_MAX, SSP_SCR_MAX);

	/* frequencies out of range get the closest reachable setting */
	if (speed >= max) {
		*cpsr = SSP_CPSR_MIN;
		*scr = SSP_SCR_MIN;
		return;
	}
	if (speed <= min) {
		*cpsr = SSP_CPSR_MAX;
		*scr = SSP_SCR_MAX;
		return;
	}

	/*
	 * Pick the highest reachable frequency NOT exceeding speed_hz:
	 * the SPI core treats speed_hz as an upper bound, so rounding up
	 * is not allowed. A candidate <= speed always exists in this range
	 * because (CPSR_MAX, SCR_MAX) yields min, and min < speed here.
	 */
	for (c = SSP_CPSR_MIN; c <= SSP_CPSR_MAX && !found; c += 2) {
		for (sc = SSP_SCR_MIN; sc <= SSP_SCR_MAX; sc++) {
			tmp = spi_rate(rate, c, sc);
			if (tmp > speed)
				continue;
			if (tmp > best) {
				best = tmp;
				best_cpsr = c;
				best_scr = sc;
				if (tmp == speed) {
					found = true;
					break;
				}
			}
		}
	}

	*cpsr = best_cpsr;
	*scr = best_scr;
}

/*
 * Program the CPSR prescaler for speed_hz and return the matching (cpsr,
 * scr) divider pair, so every caller assembles a complete CR0 in a single
 * write: the pair returned is always the pair actually programmed, which
 * rules out a CPSR(new) x SCR(old) mismatch.
 *
 * The SSP must be disabled when calling this function, since it writes
 * CPSR. It never touches CR0 nor the SSE bit: configuring CR0 and the
 * enable state stay the caller's job.
 */
static void sf_exspi_set_speed(struct sf_exspi *s, u32 speed_hz,
			       u16 *cpsr, u16 *scr) {
	if (speed_hz == s->cur_freq) {
		*cpsr = s->cur_cpsr;
		*scr = s->cur_scr;
		return;
	}

	sf_exspi_calc_div(s, speed_hz, cpsr, scr);
	writew(*cpsr, s->base + SSP_CPSR);
	s->cur_freq = speed_hz;
	s->cur_cpsr = *cpsr;
	s->cur_scr = *scr;
}

static void sf_exspi_set_param(struct sf_exspi *s, const struct spi_mem_op *op) {
	unsigned int tx_count = 0, rx_count = 0;
	u8 cmd_io, addr_io, data_io;
	u8 cmd_count, addr_count, ehc_count;
	//	static const u8 io_tb[5] = {0, 1, 2, 3, 3};
	//printk("===============data.buswidth = %i=================\n", op->data.buswidth);
	cmd_io = op->cmd.buswidth == 4 ? 3 : op->cmd.buswidth;
	addr_io = op->addr.buswidth == 4 ? 3 : op->addr.buswidth;
	data_io = op->data.buswidth == 4 ? 3 : op->data.buswidth;
	//printf("%s: cmd_io[0x%x], addr_io[0x%x], data_io[0x%x]\n",
	//	   __func__, cmd_io, addr_io, data_io);
	if (op->data.nbytes) {
		if (op->data.dir == SPI_MEM_DATA_IN) {
			rx_count = op->data.nbytes;
		} else {
			tx_count = op->data.nbytes;
		}
	}
	if (op->addr.nbytes > 3) {
		addr_count = 3;
		ehc_count = 1;
	} else {
		addr_count = op->addr.nbytes;
		ehc_count = 0;
	}
	cmd_count = op->cmd.nbytes;
	//printk("%s: cmd_len[0x%x], addr_len[0x%x], ehc_len[0x%x], dummy_len[0x%x]\n",
	//	   __func__, op->cmd.nbytes, addr_count, ehc_count, op->dummy.nbytes);
	//printk("%s: tx_count[0x%x], rx_count[0x%x]\n",
	//	   __func__, tx_count, rx_count);
	writew(FIELD_PREP(EXSPI_CMD2_CMD_IO_MODE, cmd_io) |
	       FIELD_PREP(EXSPI_CMD2_ADDR_IO_MODE, addr_io) |
	       FIELD_PREP(EXSPI_CMD2_DATA_IO_MODE, data_io),
	       s->base + SSP_EXSPI_CMD2);
	writew(FIELD_PREP(EXSPI_CMD1_DUMMY_COUNT, op->dummy.nbytes) |
	       FIELD_PREP(EXSPI_CMD1_RX_COUNT, rx_count),
	       s->base + SSP_EXSPI_CMD1);
	writew(EXSPI_CMD0_VALID |
	       FIELD_PREP(EXSPI_CMD0_CMD_COUNT, op->cmd.nbytes) |
	       FIELD_PREP(EXSPI_CMD0_ADDR_COUNT, addr_count) |
	       FIELD_PREP(EXSPI_CMD0_EHC_COUNT, ehc_count) |
	       FIELD_PREP(EXSPI_CMD0_TX_COUNT, tx_count),
	       s->base + SSP_EXSPI_CMD0);
}
static int sf_exspi_exec_op(struct spi_mem *mem,
							const struct spi_mem_op *op) {
	struct sf_exspi *s = spi_controller_get_devdata(mem->spi->master);
	unsigned int pops = 0;
	int ret, i, op_len;
	const u8 *tx_buf = NULL;
	u8 *rx_buf = NULL, op_buf[MAX_S_BUF];
	u16 cpsr, scr, cr0;

	mutex_lock(&s->lock);
	/* both paths fully reconfigure from the disabled state, so exec_op
	 * and transfer_one may interleave freely */
	sf_exspi_disable(s);
	sf_exspi_set_speed(s, mem->spi->max_speed_hz, &cpsr, &scr);
	/* rebuild the whole EXSPI frame CR0 from scratch: SPO/SPH must not
	 * keep leftovers from a preceding Motorola transfer in mode 2/3,
	 * and the mode always comes from the device being addressed */
	cr0 = SSP_CR0_EXSPI_FRAME | SSP_CR0_BIT_MODE(8);
	cr0 |= scr << SSP_SCR_SHFT;
	if (mem->spi->mode & SPI_CPHA)
		cr0 |= SSP_CR0_SPH;
	if (mem->spi->mode & SPI_CPOL)
		cr0 |= SSP_CR0_SPO;
	writew(cr0, s->base + SSP_CR0);
	/*TODO:???wait for the controller being ready*/
	if (op->data.nbytes) {
		if (op->data.dir == SPI_MEM_DATA_IN) {
			rx_buf = op->data.buf.in;
		} else {
			tx_buf = op->data.buf.out;
		}
	}
	op_len = op->cmd.nbytes + op->addr.nbytes + op->dummy.nbytes;
	sf_exspi_set_param(s, op);
	/*
	 * Avoid using malloc() here so that we can use this code in SPL where
	 * simple malloc may be used. That implementation does not allow free()
	 * so repeated calls to this code can exhaust the space.
	 *
	 * The value of op_len is small, since it does not include the actual
	 * data being sent, only the op-code and address. In fact, it should be
	 * popssible to just use a small fixed value here instead of op_len.
	 */
	op_buf[pops++] = op->cmd.opcode;
	if (op->addr.nbytes) {
		for (i = 0; i < op->addr.nbytes; i++)
			op_buf[pops + i] = op->addr.val >>
							  (8 * (op->addr.nbytes - i - 1));
		pops += op->addr.nbytes;
	}
	spi_cs_set_activate(s, mem->spi->chip_select, true);
	sf_exspi_flush_rxfifo(s);
	memset(op_buf + pops, 0xff, op->dummy.nbytes);
	sf_exspi_xmit(s, op_len, op_buf);
	if (tx_buf) {
		sf_exspi_xmit(s, op->data.nbytes, tx_buf);
	}
	sf_exspi_enable(s);
	if (rx_buf) {
		ret = sf_exspi_rcv(s, op->data.nbytes, rx_buf);
	} else {
		ret = sf_exspi_wait_not_busy(s);
	}
	spi_cs_set_activate(s, mem->spi->chip_select, false);
	sf_exspi_disable(s);
	mutex_unlock(&s->lock);
	return ret;
}

static int sf_exspi_adjust_op_size(struct spi_mem *mem,
								   struct spi_mem_op *op) {
	u32 nbytes;

	nbytes = op->cmd.nbytes + op->addr.nbytes + op->dummy.nbytes;
	if (nbytes >= SF_SSP_FIFO_LEVEL)
		return -ENOTSUPP;

	if (op->data.dir == SPI_MEM_DATA_IN)
		op->data.nbytes = min_t(unsigned int, op->data.nbytes,
					SF_SSP_FIFO_LEVEL);
	else
		op->data.nbytes = min_t(unsigned int, op->data.nbytes,
					SF_SSP_FIFO_LEVEL - nbytes);

	return 0;
}

static void sf_exspi_default_setup(struct sf_exspi *s) {
	//int ret;
	//clk_disable_unprepare(s->clk);
	//ret = clk_set_rate(s->clk, 66000000);
	u16 cpsr, scr, cr0;

	sf_exspi_disable(s);
	sf_exspi_calc_div(s, s->freq, &cpsr, &scr);

	writew(cpsr, s->base + SSP_CPSR);
	/* full rewrite from scratch in mode 0: this is only the probe/resume
	 * initial state, every real transfer path reconfigures CR0
	 * completely for the addressed device */
	cr0 = SSP_CR0_EXSPI_FRAME | SSP_CR0_BIT_MODE(8);
	cr0 |= scr << SSP_SCR_SHFT;
	writew(cr0, s->base + SSP_CR0);
	/*clear and enable interrupt*/
	//writew(0x0, s->base + SSP_RIS);
	writew(FIELD_PREP(SSP_FIFO_LEVEL_RX, DFLT_THRESH_RX) |
	       FIELD_PREP(SSP_FIFO_LEVEL_TX, DFLT_THRESH_TX),
	       s->base + SSP_FIFO_LEVEL);

	s->cur_freq = s->freq;
	s->cur_cpsr = cpsr;
	s->cur_scr = scr;
}

static const char *sf_exspi_get_name(struct spi_mem *mem) {
	struct sf_exspi *q = spi_controller_get_devdata(mem->spi->master);
	struct device *dev = &mem->spi->dev;
	const char *name;

	if (of_get_available_child_count(q->dev->of_node) == 1)
		return dev_name(q->dev);

	name = devm_kasprintf(dev, GFP_KERNEL,
						  "%s-%d", dev_name(q->dev),
						  mem->spi->chip_select);

	if (!name) {
		dev_err(dev, "failed to get memory for custom flash name\n");
		return ERR_PTR(-ENOMEM);
	}

	return name;
}

static int sf_exspi_transfer_one(struct spi_controller *ctlr,
								 struct spi_device *spi,
								 struct spi_transfer *xfer) {
	struct sf_exspi *s = spi_controller_get_devdata(ctlr);
	const u8 *tx = xfer->tx_buf;
	u8 *rx = xfer->rx_buf;
	unsigned int nbytes = xfer->len;
	unsigned int bits;
	u32 speed;
	u16 cpsr, scr, cr0;
	int ret = 0;

	mutex_lock(&s->lock);

	sf_exspi_disable(s);
	sf_exspi_flush_rxfifo(s);

	bits = xfer->bits_per_word ? : spi->bits_per_word;
	if (bits != 8) {
		dev_err(&spi->dev, "only 8-bit words are supported\n");
		ret = -EINVAL;
		goto out;
	}

	/* Motorola frame mode for plain full-duplex transfers; CS around
	 * the message is owned by the core, frequency is per transfer */
	speed = xfer->speed_hz ? : spi->max_speed_hz;
	sf_exspi_set_speed(s, speed, &cpsr, &scr);
	/* single complete CR0 write: Motorola frame (FRF=00), 8-bit words,
	 * SPO/SPH from the device mode, SCR from set_speed */
	cr0 = SSP_CR0_BIT_MODE(8) | (scr << SSP_SCR_SHFT);
	if (spi->mode & SPI_CPOL)
		cr0 |= SSP_CR0_SPO;
	if (spi->mode & SPI_CPHA)
		cr0 |= SSP_CR0_SPH;
	writew(cr0, s->base + SSP_CR0);

	sf_exspi_enable(s);
	while (nbytes--) {
		u8 byte = tx ? *tx++ : 0;

		ret = sf_exspi_wait_txfifo_not_full(s);
		if (ret)
			goto out;

		writew(byte, s->base + SSP_DR);

		ret = sf_exspi_wait_rx_not_empty(s);
		if (ret)
			goto out;

		/* always drain the RX FIFO, dropping data when the transfer
		 * has no rx buffer, so the FIFO never overruns */
		if (rx)
			*rx++ = readw(s->base + SSP_DR);
		else
			readw(s->base + SSP_DR);
	}

	ret = sf_exspi_wait_not_busy(s);
out:
	sf_exspi_disable(s);
	mutex_unlock(&s->lock);
	return ret;
}

static const struct spi_controller_mem_ops sf_exspi_mem_ops = {
	.adjust_op_size = sf_exspi_adjust_op_size,
	.exec_op = sf_exspi_exec_op,
	.get_name = sf_exspi_get_name,
};

static int sf_exspi_csgpio_mode_set(struct device *dev, struct sf_exspi *s)
{
	s->cs_gpio = devm_gpiod_get_array_optional(dev, "cs", GPIOD_OUT_LOW);
	if (IS_ERR(s->cs_gpio))
		return PTR_ERR(s->cs_gpio);

	return 0;
}

static int sf_exspi_probe(struct platform_device *pdev) {
	struct spi_master *master;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node, *nc;
	struct resource *res;
	struct sf_exspi *s;
	int ret;
	u32 num_cs;

	master = devm_spi_alloc_master(&pdev->dev, sizeof(*s));
	if (!master)
		return -ENOMEM;
	master->mode_bits = SPI_CPOL | SPI_CPHA |
			   SPI_RX_DUAL | SPI_RX_QUAD |
			   SPI_TX_DUAL | SPI_TX_QUAD;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	s = spi_controller_get_devdata(master);
	s->dev = dev;
	platform_set_drvdata(pdev, s);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	s->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(s->base)) {
		ret = PTR_ERR(s->base);
		goto err_put_master;
	}
	s->clk = devm_clk_get(dev, "sspclk");
	if (IS_ERR(s->clk)) {
		ret = PTR_ERR(s->clk);
		goto err_put_master;
	}
	s->apbclk = devm_clk_get(dev, "apb_pclk");
	if (IS_ERR(s->apbclk)) {
		ret = PTR_ERR(s->apbclk);
		goto err_put_master;
	}

	ret = clk_prepare_enable(s->apbclk);
	if (ret) {
		dev_err(dev, "cannot enable apb clock\n");
		goto err_put_master;
	}

	ret = clk_prepare_enable(s->clk);
	if (ret) {
		dev_err(dev, "cannot enable ssp clock\n");
		goto err_disable_apbclk;
	}

	for_each_available_child_of_node(dev->of_node, nc) {
		of_property_read_u32(nc, "spi-max-frequency", &s->freq);
	}
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto err_disable_clk;
	ret = devm_request_irq(dev, ret, sf_exspi_irq_handler, 0, pdev->name, s);
	if (ret) {
		dev_err(dev, "failed to request irq: %d\n", ret);
		goto err_disable_clk;
	}
	mutex_init(&s->lock);
	master->bus_num = pdev->id;
	if (of_property_read_u32(np, "num-cs", &num_cs))
		master->num_chipselect = 1;
	else
		master->num_chipselect = num_cs;
	master->mem_ops = &sf_exspi_mem_ops;
	master->transfer_one = sf_exspi_transfer_one;
	sf_exspi_default_setup(s);
	master->dev.of_node = np;
	ret = sf_exspi_csgpio_mode_set(dev, s);
	if (ret)
		goto err_destroy_mutex;

	ret = devm_spi_register_controller(dev, master);
	if (ret)
		goto err_destroy_mutex;
	return 0;
err_destroy_mutex:
	mutex_destroy(&s->lock);
err_disable_clk:
	clk_disable_unprepare(s->clk);
err_disable_apbclk:
	clk_disable_unprepare(s->apbclk);
err_put_master:

	dev_err(dev, "SF_EXSPI probe failed\n");
	return ret;
}

static int sf_exspi_remove(struct platform_device *pdev) {
	struct sf_exspi *s = platform_get_drvdata(pdev);
	/*TODO:disable the hardware*/
	clk_disable_unprepare(s->clk);
	clk_disable_unprepare(s->apbclk);
	mutex_destroy(&s->lock);
	return 0;
}

static int sf_exspi_suspend(struct device *dev) {
	return 0;
}

static int sf_exspi_resume(struct device *dev) {
	struct sf_exspi *s = dev_get_drvdata(dev);

	sf_exspi_default_setup(s);

	return 0;
}

static const struct of_device_id sf_exspi_ids[] = {
	{.compatible = "siflower,sf-exspi"},
	{},
};
MODULE_DEVICE_TABLE(of, sf_exspi_ids);

static const struct dev_pm_ops sf_exspi_pm_ops = {
	.suspend = sf_exspi_suspend,
	.resume = sf_exspi_resume,
};
static struct platform_driver sf_exspi_driver = {
	.driver = {
		.name = "sf_exspi",
		.of_match_table = sf_exspi_ids,
		.pm = &sf_exspi_pm_ops,
	},
	.probe = sf_exspi_probe,
	.remove = sf_exspi_remove,
};
module_platform_driver(sf_exspi_driver);
