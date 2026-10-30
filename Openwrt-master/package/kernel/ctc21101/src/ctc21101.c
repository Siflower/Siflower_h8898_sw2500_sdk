#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/netdevice.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include "ctc21101.h"


static int ctcphy_read_ext(struct phy_device *phydev, u32 regnum)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __phy_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		goto err_handle;
	
	ret = __phy_read(phydev, REG_DEBUG_DATA);
	if (ret < 0)
		goto err_handle;

err_handle:
	phy_unlock_mdio_bus(phydev);

	return ret;
}

static int ctcphy_read_ext_no_lock(struct phy_device *phydev, u32 regnum)
{
    int ret;

    ret = __phy_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
    if (ret < 0)
		return ret;

    return __phy_read(phydev, REG_DEBUG_DATA);
}

static int ctcphy_write_ext(struct phy_device *phydev, u32 regnum, u16 val)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __phy_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		goto err_handle;
	
	ret = __phy_write(phydev, REG_DEBUG_DATA, val);
	if (ret < 0)
		goto err_handle;

err_handle:
	phy_unlock_mdio_bus(phydev);

	return ret;
}

static int ctcphy_write_ext_no_lock(struct phy_device *phydev, u32 regnum, u16 val)
{
    int ret;

    ret = __phy_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
    if (ret < 0)
		return ret;

    return __phy_write(phydev, REG_DEBUG_DATA, val);
}

static int ctc21101_modify_ext(struct phy_device *phydev, u16 regnum, u16 mask,
								u16 set)
{
	int ret;

	ret = __phy_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		return ret;
	
	return __phy_modify(phydev, REG_DEBUG_DATA, mask, set);
}

static int ctc21101_read_page(struct phy_device *phydev)
{
	int old_page;

	old_page = ctcphy_read_ext_no_lock(phydev, CTC21101_REG_SPACE_SELECT_REG);
	if (old_page < 0)
		return old_page;

	if ((old_page & CTC21101_RSSR_SPACE_MASK) == CTC21101_RSSR_FIBER_SPACE)
		return CTC21101_RSSR_FIBER_SPACE;

	return CTC21101_RSSR_UTP_SPACE;
}

static int ctc21101_write_page(struct phy_device *phydev, int page)
{
	int mask = CTC21101_RSSR_SPACE_MASK;
	int set;

	if ((page & CTC21101_RSSR_SPACE_MASK) == CTC21101_RSSR_FIBER_SPACE)
		set = CTC21101_RSSR_FIBER_SPACE;
	else
		set = CTC21101_RSSR_UTP_SPACE;

	return ctc21101_modify_ext(phydev, CTC21101_REG_SPACE_SELECT_REG, mask, set);
}

static int ctc21101_utp_read_lpa(struct phy_device *phydev)
{
	return 0;
}
static int ctc21101_adjust_status(struct phy_device *phydev, int val, int is_utp)
{
	int speed_mode, duplex;
	int speed;
	int err;
	int lpa;

	if (is_utp)
		duplex = (val & CTC_DUPLEX) >> CTC_DUPLEX_BIT;
	else
		duplex = DUPLEX_FULL;
	
	speed_mode = (val & CTC_SPEED_MODE) >> CTC_SPEED_MODE_BIT;
	switch (speed_mode) {
	case 0:
		if (is_utp)
			speed = SPEED_10;
		else
			speed = SPEED_UNKNOWN;
		break;
	case 1:
		speed = SPEED_100;
		break;
	case 2:
		speed = SPEED_1000;
		break;
	case 3:
		break;
	default:
		speed = SPEED_UNKNOWN;
		break;
	}

	phydev->speed = speed;
	phydev->duplex = duplex;

	if (is_utp) {
		err = ctc21101_utp_read_lpa(phydev);
		if (err < 0)
			return err;

		phy_resolve_aneg_pause(phydev);
	} else {
		lpa = __phy_read(phydev, MII_LPA);
		if (lpa < 0)
			return lpa;

		linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
				 phydev->lp_advertising, lpa & LPA_1000XFULL);

		if (!(lpa & CTC21101_FLPA_PAUSE)) {
			phydev->pause = 0;
			phydev->asym_pause = 0;
		} else if ((lpa & CTC21101_FLPA_ASYM_PAUSE)) {
			phydev->pause = 1;
			phydev->asym_pause = 1;
		} else {
			phydev->pause = 1;
			phydev->asym_pause = 0;
		}
	}

	return 0;
}

static int ctc21101_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct ctc21101_priv *priv;
	int ret, chip_config;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	
	phydev->priv = priv;

	chip_config = ctcphy_read_ext(phydev, 0xa001);
	priv->chip_mode = chip_config & 0x7;
	switch (priv->chip_mode) {
		case 1:    /* fiber<->rgmii*/
		case 4:
		case 5:
			priv->polling_mode = CTC_PHY_MODE_FIBER;
			priv->reg_page = CTC21101_RSSR_FIBER_SPACE;
			phydev->port = PORT_FIBRE;
			break;
		case 2:    /* utp/fiber<->rgmii*/
		case 6:
		case 7:
			priv->polling_mode = CTC_PHY_MODE_POLL;
			priv->reg_page = CTC21101_RSSR_TO_BE_ARBITRATED;
			phydev->port = PORT_NONE;
			break;
		case 0:    /* utp<->rgmii/sgmii*/
		case 3:
		default:
			priv->polling_mode = CTC_PHY_MODE_UTP;
			priv->reg_page = CTC21101_RSSR_UTP_SPACE;
			phydev->port = PORT_TP;
			break;
	}

	if (priv->reg_page != CTC21101_RSSR_TO_BE_ARBITRATED) {
		ret = ctcphy_write_ext(phydev, 
								CTC21101_REG_SPACE_SELECT_REG,
								priv->reg_page);
		if (ret < 0)
			return ret;
	}

	return 0;
}
static int ctc21101_modify_bmcr_paged(struct phy_device *phydev, int page,
				    u16 mask, u16 set)
{
	int max_cnt = 500;
	int old_page;
	int ret = 0;

	old_page = phy_select_page(phydev, page & CTC21101_RSSR_SPACE_MASK);
	if (old_page < 0)
		goto err_restore_page;

	ret = __phy_modify(phydev, MII_BMCR, mask, set);
	if (ret < 0)
		goto err_restore_page;

	if (set == BMCR_RESET) {
		while (max_cnt--) {
			usleep_range(1000, 1100);
			ret = __phy_read(phydev, MII_BMCR);
			if (ret < 0)
				goto err_restore_page;

			if (!(ret & BMCR_RESET))
				return phy_restore_page(phydev, old_page, 0);
		}
	}

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

static int ctc21101_soft_reset(struct phy_device *phydev)
{
	struct ctc21101_priv *priv = phydev->priv;
	int ret;

	if (priv->reg_page != CTC21101_RSSR_TO_BE_ARBITRATED) {
		ret = ctc21101_modify_bmcr_paged(phydev, priv->reg_page, 0,
					       BMCR_RESET);
		if (ret < 0)
			return ret;
	} else {
		ret = ctc21101_modify_bmcr_paged(phydev, CTC21101_RSSR_UTP_SPACE,
					       0, BMCR_RESET);
		if (ret < 0)
			return ret;

		ret = ctc21101_modify_bmcr_paged(phydev, CTC21101_RSSR_FIBER_SPACE,
					       0, BMCR_RESET);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int ctc21101_fiber_setup_forced(struct phy_device *phydev)
{
	u16 val;
	int ret;

	if (phydev->speed == SPEED_1000)
		val = CTC21101_MCR_FIBER_1000BX;
	else if (phydev->speed == SPEED_100)
		val = CTC21101_MCR_FIBER_100FX;
	else
		return -EINVAL;
	
	ret = __phy_modify(phydev, MII_BMCR, BMCR_ANENABLE, 0);
	if (ret < 0)
		return ret;
	
	ret = ctc21101_modify_ext(phydev, CTC21101_LINK_TIMER_CFG2_REG,
							CTC21101_LTCR_EN_AUTOSEN, 0);
	if (ret < 0)
		return ret;

	
	ret = ctc21101_modify_ext(phydev, CTC21101_MISC_CONFIG_REG,
							CTC21101_MCR_FIBER_SPEED_MASK, val);
	if (ret < 0)
		return ret;

	return ctc21101_modify_ext(phydev, CTC21101_CHIP_CONFIG_REG,
							CTC21101_CCR_SW_RST, 0);
}

static int ctc21101_check_and_restart_aneg(struct phy_device *phydev, bool restart)
{
	int ret;

	if (!restart) {
		ret = __phy_read(phydev, MII_BMCR);
		if (ret < 0)
			return ret;

		if (!(ret & BMCR_ANENABLE) || (ret & BMCR_ISOLATE))
			restart = true;
	}

	if (restart)
		return __phy_modify(phydev, MII_BMCR, BMCR_ISOLATE,
					BMCR_ANENABLE | BMCR_ANRESTART);

	return 0;
}

static int ctc21101_fiber_config_aneg(struct phy_device *phydev)
{
	int err, changed = 0;
	int bmcr;
	u16 adv;

	if (phydev->autoneg != AUTONEG_ENABLE)
		return ctc21101_fiber_setup_forced(phydev);

	err =  ctc21101_modify_ext(phydev, CTC21101_LINK_TIMER_CFG2_REG,
				0, CTC21101_LTCR_EN_AUTOSEN);
	if (err < 0)
		return err;

	err =  ctc21101_modify_ext(phydev, CTC21101_CHIP_CONFIG_REG,
				CTC21101_CCR_SW_RST, 0);
	if (err < 0)
		return err;

	bmcr = __phy_read(phydev, MII_BMCR);
	if (bmcr < 0)
		return bmcr;

	if (!(bmcr & BMCR_ANENABLE)) {
		__phy_modify(phydev, MII_BMCR, 0, BMCR_PDOWN);
		usleep_range(1000, 1100);
		__phy_modify(phydev, MII_BMCR, BMCR_PDOWN, 0);
	}

	adv = linkmode_adv_to_mii_adv_x(phydev->advertising,
					ETHTOOL_LINK_MODE_1000baseX_Full_BIT);

	err = __phy_modify_changed(phydev, MII_ADVERTISE,
				   ADVERTISE_1000XHALF | ADVERTISE_1000XFULL |
				   ADVERTISE_1000XPAUSE |
				   ADVERTISE_1000XPSE_ASYM,
				   adv);
	if (err < 0)
		return err;

	if (err > 0)
		changed = 1;

	return ctc21101_check_and_restart_aneg(phydev, changed);
}

static void ctc21101_prepare_fiber_features(struct phy_device *phydev,
										unsigned long *dst)
{
	linkmode_set_bit(ETHTOOL_LINK_MODE_100baseFX_Full_BIT, dst);
	linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseX_Full_BIT, dst);
	linkmode_set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, dst);
	linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT, dst);
}

static int ctc21101_utp_config_aneg(struct phy_device *phydev, bool changed)
{
	return 0;
}

static int ctc21101_config_aneg_paged(struct phy_device *phydev, int page)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(fiber_supported);
	struct ctc21101_priv *priv = phydev->priv;
	int old_page;
	int ret = 0;

	page &= CTC21101_RSSR_SPACE_MASK;

	old_page = phy_select_page(phydev, page);
	if (old_page < 0)
		goto err_restore_page;

	if (priv->reg_page == CTC21101_RSSR_TO_BE_ARBITRATED) {
		linkmode_zero(fiber_supported);
		ctc21101_prepare_fiber_features(phydev, fiber_supported);

		if (page == CTC21101_RSSR_FIBER_SPACE) {
			linkmode_set_bit(ETHTOOL_LINK_MODE_Pause_BIT,
					 fiber_supported);
			linkmode_set_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
					 fiber_supported);
			linkmode_and(phydev->advertising,
				     priv->combo_advertising, fiber_supported);
		} else {
			linkmode_clear_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
					   fiber_supported);
			linkmode_andnot(phydev->advertising,
					priv->combo_advertising,
					fiber_supported);
		}
	}

	if (page == CTC21101_RSSR_FIBER_SPACE)
		ret = ctc21101_fiber_config_aneg(phydev);
	else
		ret = ctc21101_utp_config_aneg(phydev, false);

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

static int ctc21101_config_aneg(struct phy_device *phydev)
{
	struct ctc21101_priv *priv = phydev->priv;
	int ret;

	if (priv->reg_page != CTC21101_RSSR_TO_BE_ARBITRATED) {
		ret = ctc21101_config_aneg_paged(phydev, priv->reg_page);
		if (ret < 0)
			return ret;
	} else {
		if (linkmode_empty(priv->combo_advertising)) {
			linkmode_copy(priv->combo_advertising,
					  phydev->advertising);
		}

		ret = ctc21101_config_aneg_paged(phydev, CTC21101_RSSR_UTP_SPACE);
		if (ret < 0)
			return ret;

		ret = ctc21101_config_aneg_paged(phydev, CTC21101_RSSR_FIBER_SPACE);
		if (ret < 0)
			return ret;

		linkmode_copy(phydev->advertising, priv->combo_advertising);
	}
	return 0;
}

static int ctc21101_aneg_done_paged(struct phy_device *phydev, int page)
{
	int old_page;
	int ret = 0;
	int link;

	old_page = phy_select_page(phydev, page & CTC21101_RSSR_SPACE_MASK);
	if (old_page < 0)
		goto err_restore_page;

	ret = __phy_read(phydev, REG_PHY_SPEC_STATUS);
	if (ret < 0)
		goto err_restore_page;

	link = !!(ret & BIT(CTC_LINK_STATUS_BIT));
	ret = link;

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

static int ctc21101_aneg_done(struct phy_device *phydev)
{
	struct ctc21101_priv *priv = phydev->priv;
	int link_fiber = 0;
	int link_utp;
	int link;

	if (priv->reg_page != CTC21101_RSSR_TO_BE_ARBITRATED) {
		link = ctc21101_aneg_done_paged(phydev, priv->reg_page);
	} else {
		link_utp = ctc21101_aneg_done_paged(phydev,
						  CTC21101_RSSR_UTP_SPACE);
		if (link_utp < 0)
			return link_utp;

		if (!link_utp) {
			link_fiber = ctc21101_aneg_done_paged(phydev,
							    CTC21101_RSSR_FIBER_SPACE);
			if (link_fiber < 0)
				return link_fiber;
		}
		link = link_fiber || link_utp;
		netdev_info(phydev->attached_dev,
			"%s, phy addr: %d, link fiber:%d, link utp: %d\n",
			__func__, phydev->mdio.addr, link_fiber, link_utp);
	}

	return link;
}

static int ctc21101_config_init(struct phy_device *phydev)
{
	int old_page;
	int ret = 0;
	struct ctc21101_priv *priv = phydev->priv;

	old_page = phy_select_page(phydev, CTC21101_RSSR_UTP_SPACE);
	if (old_page < 0)
		goto err_restore_page;

	ret = ctc21101_modify_ext(phydev, CTC21101_CHIP_CONFIG_REG,
				   CTC21101_CCR_RXC_DLY_EN, 0);
	if (ret < 0)
		goto err_restore_page;

	ret = ctc21101_modify_ext(phydev, CTC21101_RGMII_CONFIG1,
				   CTC21101_RC1R_GE_TX_DELAY_MASK, 0xA);
	if (ret < 0)
		goto err_restore_page;

	ret = ctc21101_modify_ext(phydev, CTC21101_EXTREG_SLEEP_CONTROL1,
				   BIT(CTC21101_EN_SLEEP_SW_BIT), 0);
	if (ret < 0)
		goto err_restore_page;
	
	ret = ctc21101_modify_ext(phydev, CTC21101_CLOCK_GATING_REG,
				   CTC21101_CGR_RX_CLK_EN, 0);
	if (ret < 0)
		goto err_restore_page;

	if (priv->reg_page == CTC21101_RSSR_FIBER_SPACE) {
		phydev->autoneg = AUTONEG_DISABLE;
		phydev->speed = SPEED_1000;
		phydev->duplex = DUPLEX_FULL;
	}
	
	netdev_info(phydev->attached_dev,
		"%s done, phy addr: %d, chip mode = %d, polling mode = %d\n",
		__func__, phydev->mdio.addr,
		priv->chip_mode, priv->polling_mode);

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);	
}

static int ctc21101_read_status_paged(struct phy_device *phydev, int page)
{
	int fiber_latch_val;
	int fiber_curr_val;
	int old_page;
	int ret = 0;
	int status;
	int link;

	linkmode_zero(phydev->lp_advertising);
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->speed = SPEED_UNKNOWN;
	phydev->asym_pause = 0;
	phydev->pause = 0;

	page &= CTC21101_RSSR_SPACE_MASK;
	old_page = phy_select_page(phydev, page);
	if (old_page < 0)
		goto err_restore_page;

	ret = __phy_read(phydev, REG_PHY_SPEC_STATUS);
	if (ret < 0)
		goto err_restore_page;

	status = ret;
	link = !!(status & BIT(CTC_LINK_STATUS_BIT));

	if (page == CTC21101_RSSR_FIBER_SPACE) {
		ret = __phy_read(phydev, MII_BMSR);
		if (ret < 0)
			goto err_restore_page;

		fiber_latch_val = ret;
		ret = __phy_read(phydev, MII_BMSR);
		if (ret < 0)
			goto err_restore_page;

		fiber_curr_val = ret;
		if (link && fiber_latch_val != fiber_curr_val) {
			link = 0;
			phydev_info(phydev,
				    "%s, fiber link down detect, latch = %04x, curr = %04x\n",
				    __func__, fiber_latch_val, fiber_curr_val);
		}
	} else {
		ret = __phy_read(phydev, MII_BMSR);
		if (ret < 0)
			goto err_restore_page;

		phydev->autoneg_complete = ret & BMSR_ANEGCOMPLETE ? 1 : 0;
	}

	if (link) {
		if (page == CTC21101_RSSR_UTP_SPACE)
			ctc21101_adjust_status(phydev, status, true);
		else
			ctc21101_adjust_status(phydev, status, false);
	}
	return phy_restore_page(phydev, old_page, link);

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

static int ctc21101_read_status(struct phy_device *phydev)
{
	struct ctc21101_priv *priv = phydev->priv;
	int link_fiber = 0;
	int link_utp;
	int link;
	int ret;

	if (priv->reg_page != CTC21101_RSSR_TO_BE_ARBITRATED) {
		link = ctc21101_read_status_paged(phydev, priv->reg_page);
		if (link < 0)
			return link;
	} else {

		link_utp = ctc21101_read_status_paged(phydev,
						    CTC21101_RSSR_UTP_SPACE);
		if (link_utp < 0)
			return link_utp;

		if (!link_utp) {
			link_fiber = ctc21101_read_status_paged(phydev,
							      CTC21101_RSSR_FIBER_SPACE);
			if (link_fiber < 0)
				return link_fiber;
		}

		link = link_utp || link_fiber;
	}

	if (link) {
		if (phydev->link == 0) {
			if (priv->polling_mode == CTC_PHY_MODE_POLL &&
			    priv->reg_page == CTC21101_RSSR_TO_BE_ARBITRATED) {
				if (link_fiber)
					priv->reg_page =
						CTC21101_RSSR_FIBER_SPACE;
				else
					priv->reg_page = CTC21101_RSSR_UTP_SPACE;

				ret = ctcphy_write_ext(phydev,
								CTC21101_REG_SPACE_SELECT_REG,
								priv->reg_page);
				if (ret < 0)
					return ret;

				phydev->port = link_fiber ? PORT_FIBRE : PORT_TP;

				phydev_info(phydev, "%s, link up, media: %s\n",
					    __func__,
					    (phydev->port == PORT_TP) ?
					    "UTP" : "Fiber");
			}
		}
		phydev->link = 1;
	} else {
		if (phydev->link == 1) {
			phydev_info(phydev, "%s, link down, media: %s\n",
				    __func__, (phydev->port == PORT_TP) ?
				    "UTP" : "Fiber");

			if (priv->polling_mode == CTC_PHY_MODE_POLL) {
				priv->reg_page = CTC21101_RSSR_TO_BE_ARBITRATED;
				phydev->port = PORT_NONE;
			}
		}

		phydev->link = 0;
	}

	return 0;
}

static int ctc21101_utp_read_abilities(struct phy_device *phydev)
{
	return 0;
}

static int ctc21101_get_features_paged(struct phy_device *phydev, int page)
{
	int old_page;
	int ret = 0;

	page &= CTC21101_RSSR_SPACE_MASK;
	old_page = phy_select_page(phydev, page);
	if (old_page < 0)
		goto err_restore_page;

	if (page == CTC21101_RSSR_FIBER_SPACE) {
		linkmode_zero(phydev->supported);
		ctc21101_prepare_fiber_features(phydev, phydev->supported);
	} else {
		ret = ctc21101_utp_read_abilities(phydev);
		if (ret < 0)
			goto err_restore_page;
	}

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

static int ctc21101_get_features(struct phy_device *phydev)
{
	struct ctc21101_priv *priv = phydev->priv;
	int ret;

	if (priv->reg_page != CTC21101_RSSR_TO_BE_ARBITRATED) {
		ret = ctc21101_get_features_paged(phydev, priv->reg_page);
	} else {
		ret = ctc21101_get_features_paged(phydev,
						CTC21101_RSSR_UTP_SPACE);
		if (ret < 0)
			return ret;

		/* add fiber's features to phydev->supported */
		ctc21101_prepare_fiber_features(phydev, phydev->supported);
	}
	return ret;
}

static struct phy_driver ctc21101_driver[] = {
{
	.phy_id        = CTC21101_PHY_ID,
	.name          = "CTC21101 PHY",
	.phy_id_mask   = 0xffffffff,
	.get_features  = ctc21101_get_features,
	.probe         = ctc21101_probe,
	.read_page     = ctc21101_read_page,
	.write_page    = ctc21101_write_page,
	.soft_reset    = ctc21101_soft_reset,
	.config_aneg   = ctc21101_config_aneg,
	.aneg_done     = ctc21101_aneg_done,
	.config_init   = ctc21101_config_init,
	.read_status   = ctc21101_read_status,
},};

module_phy_driver(ctc21101_driver);

MODULE_DESCRIPTION("CTC21101 PHY Driver");
MODULE_AUTHOR("Siflower");
MODULE_LICENSE("GPL");

static const struct mdio_device_id __maybe_unused ctc21101_tbl[] = {
	{PHY_ID_MATCH_EXACT(CTC21101_PHY_ID)},
	{}
};

MODULE_DEVICE_TABLE(mdio, ctc21101_tbl);
