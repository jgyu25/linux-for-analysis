// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2020 Intel Corporation */
#include "adf_common_drv.h"
#include "adf_dc.h"
#include "adf_gen2_hw_data.h"
#include "icp_qat_fw_comp.h"
#include "icp_qat_hw.h"
#include <linux/pci.h>

u32 adf_gen2_get_num_accels(struct adf_hw_device_data *self)
{
	if (!self || !self->accel_mask)
		return 0;

	return hweight16(self->accel_mask);
}
EXPORT_SYMBOL_GPL(adf_gen2_get_num_accels);

u32 adf_gen2_get_num_aes(struct adf_hw_device_data *self)
{
	if (!self || !self->ae_mask)
		return 0;

	return hweight32(self->ae_mask);
}
EXPORT_SYMBOL_GPL(adf_gen2_get_num_aes);

void adf_gen2_enable_error_correction(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *pmisc_addr = adf_get_pmisc_base(accel_dev);
	unsigned long accel_mask = hw_data->accel_mask;
	unsigned long ae_mask = hw_data->ae_mask;
	unsigned int val, i;

	/* Enable Accel Engine error detection & correction */
	for_each_set_bit(i, &ae_mask, hw_data->num_engines) {
		val = ADF_CSR_RD(pmisc_addr, ADF_GEN2_AE_CTX_ENABLES(i));
		val |= ADF_GEN2_ENABLE_AE_ECC_ERR;
		ADF_CSR_WR(pmisc_addr, ADF_GEN2_AE_CTX_ENABLES(i), val);
		val = ADF_CSR_RD(pmisc_addr, ADF_GEN2_AE_MISC_CONTROL(i));
		val |= ADF_GEN2_ENABLE_AE_ECC_PARITY_CORR;
		ADF_CSR_WR(pmisc_addr, ADF_GEN2_AE_MISC_CONTROL(i), val);
	}

	/* Enable shared memory error detection & correction */
	for_each_set_bit(i, &accel_mask, hw_data->num_accel) {
		val = ADF_CSR_RD(pmisc_addr, ADF_GEN2_UERRSSMSH(i));
		val |= ADF_GEN2_ERRSSMSH_EN;
		ADF_CSR_WR(pmisc_addr, ADF_GEN2_UERRSSMSH(i), val);
		val = ADF_CSR_RD(pmisc_addr, ADF_GEN2_CERRSSMSH(i));
		val |= ADF_GEN2_ERRSSMSH_EN;
		ADF_CSR_WR(pmisc_addr, ADF_GEN2_CERRSSMSH(i), val);
	}
}
EXPORT_SYMBOL_GPL(adf_gen2_enable_error_correction);

void adf_gen2_cfg_iov_thds(struct adf_accel_dev *accel_dev, bool enable,
			   int num_a_regs, int num_b_regs)
{
	void __iomem *pmisc_addr = adf_get_pmisc_base(accel_dev);
	u32 reg;
	int i;

	/* Set/Unset Valid bit in AE Thread to PCIe Function Mapping Group A */
	for (i = 0; i < num_a_regs; i++) {
		reg = READ_CSR_AE2FUNCTION_MAP_A(pmisc_addr, i);
		if (enable)
			reg |= AE2FUNCTION_MAP_VALID;
		else
			reg &= ~AE2FUNCTION_MAP_VALID;
		WRITE_CSR_AE2FUNCTION_MAP_A(pmisc_addr, i, reg);
	}

	/* Set/Unset Valid bit in AE Thread to PCIe Function Mapping Group B */
	for (i = 0; i < num_b_regs; i++) {
		reg = READ_CSR_AE2FUNCTION_MAP_B(pmisc_addr, i);
		if (enable)
			reg |= AE2FUNCTION_MAP_VALID;
		else
			reg &= ~AE2FUNCTION_MAP_VALID;
		WRITE_CSR_AE2FUNCTION_MAP_B(pmisc_addr, i, reg);
	}
}
EXPORT_SYMBOL_GPL(adf_gen2_cfg_iov_thds);

void adf_gen2_get_admin_info(struct admin_info *admin_csrs_info)
{
	admin_csrs_info->mailbox_offset = ADF_MAILBOX_BASE_OFFSET;
	admin_csrs_info->admin_msg_ur = ADF_ADMINMSGUR_OFFSET;
	admin_csrs_info->admin_msg_lr = ADF_ADMINMSGLR_OFFSET;
}
EXPORT_SYMBOL_GPL(adf_gen2_get_admin_info);

void adf_gen2_get_arb_info(struct arb_info *arb_info)
{
	arb_info->arb_cfg = ADF_ARB_CONFIG;
	arb_info->arb_offset = ADF_ARB_OFFSET;
	arb_info->wt2sam_offset = ADF_ARB_WRK_2_SER_MAP_OFFSET;
}
EXPORT_SYMBOL_GPL(adf_gen2_get_arb_info);

void adf_gen2_enable_ints(struct adf_accel_dev *accel_dev)
{
	void __iomem *addr = adf_get_pmisc_base(accel_dev);
	u32 val;

	val = accel_dev->pf.vf_info ? 0 : BIT_ULL(GET_MAX_BANKS(accel_dev)) - 1;

	/* Enable bundle and misc interrupts */
	ADF_CSR_WR(addr, ADF_GEN2_SMIAPF0_MASK_OFFSET, val);
	ADF_CSR_WR(addr, ADF_GEN2_SMIAPF1_MASK_OFFSET, ADF_GEN2_SMIA1_MASK);
}
EXPORT_SYMBOL_GPL(adf_gen2_enable_ints);

u32 adf_gen2_get_accel_cap(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct pci_dev *pdev = accel_dev->accel_pci_dev.pci_dev;
	u32 fuses = hw_data->fuses[ADF_FUSECTL0];
	u32 straps = hw_data->straps;
	u32 legfuses;
	u32 capabilities = ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC |
			   ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC |
			   ICP_ACCEL_CAPABILITIES_AUTHENTICATION |
			   ICP_ACCEL_CAPABILITIES_CIPHER |
			   ICP_ACCEL_CAPABILITIES_COMPRESSION;

	/* Read accelerator capabilities mask */
	pci_read_config_dword(pdev, ADF_DEVICE_LEGFUSE_OFFSET, &legfuses);

	/* A set bit in legfuses means the feature is OFF in this SKU */
	if (legfuses & ICP_ACCEL_MASK_CIPHER_SLICE) {
		capabilities &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC;
		capabilities &= ~ICP_ACCEL_CAPABILITIES_CIPHER;
	}
	if (legfuses & ICP_ACCEL_MASK_PKE_SLICE)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;
	if (legfuses & ICP_ACCEL_MASK_AUTH_SLICE) {
		capabilities &= ~ICP_ACCEL_CAPABILITIES_AUTHENTICATION;
		capabilities &= ~ICP_ACCEL_CAPABILITIES_CIPHER;
	}
	if (legfuses & ICP_ACCEL_MASK_COMPRESS_SLICE)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_COMPRESSION;

	if ((straps | fuses) & ADF_POWERGATE_PKE)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;

	if ((straps | fuses) & ADF_POWERGATE_DC)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_COMPRESSION;

	return capabilities;
}
EXPORT_SYMBOL_GPL(adf_gen2_get_accel_cap);

void adf_gen2_set_ssm_wdtimer(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *pmisc_addr = adf_get_pmisc_base(accel_dev);
	u32 timer_val_pke = ADF_SSM_WDT_PKE_DEFAULT_VALUE;
	u32 timer_val = ADF_SSM_WDT_DEFAULT_VALUE;
	unsigned long accel_mask = hw_data->accel_mask;
	u32 i = 0;

	/* Configures WDT timers */
	for_each_set_bit(i, &accel_mask, hw_data->num_accel) {
		/* Enable WDT for sym and dc */
		ADF_CSR_WR(pmisc_addr, ADF_SSMWDT(i), timer_val);
		/* Enable WDT for pke */
		ADF_CSR_WR(pmisc_addr, ADF_SSMWDTPKE(i), timer_val_pke);
	}
}
EXPORT_SYMBOL_GPL(adf_gen2_set_ssm_wdtimer);

static int adf_gen2_build_comp_block(void *ctx, enum adf_dc_algo algo)
{
	struct icp_qat_fw_comp_req *req_tmpl = ctx;
	struct icp_qat_fw_comp_req_hdr_cd_pars *cd_pars = &req_tmpl->cd_pars;
	struct icp_qat_fw_comn_req_hdr *header = &req_tmpl->comn_hdr;

	switch (algo) {
	case QAT_DEFLATE:
		header->service_cmd_id = ICP_QAT_FW_COMP_CMD_STATIC;
		break;
	default:
		return -EINVAL;
	}

	cd_pars->u.sl.comp_slice_cfg_word[0] =
		ICP_QAT_HW_COMPRESSION_CONFIG_BUILD(ICP_QAT_HW_COMPRESSION_DIR_COMPRESS,
						    ICP_QAT_HW_COMPRESSION_DELAYED_MATCH_DISABLED,
						    ICP_QAT_HW_COMPRESSION_ALGO_DEFLATE,
						    ICP_QAT_HW_COMPRESSION_DEPTH_1,
						    ICP_QAT_HW_COMPRESSION_FILE_TYPE_0);

	return 0;
}

static int adf_gen2_build_decomp_block(void *ctx, enum adf_dc_algo algo)
{
	struct icp_qat_fw_comp_req *req_tmpl = ctx;
	struct icp_qat_fw_comp_req_hdr_cd_pars *cd_pars = &req_tmpl->cd_pars;
	struct icp_qat_fw_comn_req_hdr *header = &req_tmpl->comn_hdr;

	switch (algo) {
	case QAT_DEFLATE:
		header->service_cmd_id = ICP_QAT_FW_COMP_CMD_DECOMPRESS;
	break;
	default:
		return -EINVAL;
	}

	cd_pars->u.sl.comp_slice_cfg_word[0] =
		ICP_QAT_HW_COMPRESSION_CONFIG_BUILD(ICP_QAT_HW_COMPRESSION_DIR_DECOMPRESS,
						    ICP_QAT_HW_COMPRESSION_DELAYED_MATCH_DISABLED,
						    ICP_QAT_HW_COMPRESSION_ALGO_DEFLATE,
						    ICP_QAT_HW_COMPRESSION_DEPTH_1,
						    ICP_QAT_HW_COMPRESSION_FILE_TYPE_0);

	return 0;
}

void adf_gen2_init_dc_ops(struct adf_dc_ops *dc_ops)
{
	dc_ops->build_comp_block = adf_gen2_build_comp_block;
	dc_ops->build_decomp_block = adf_gen2_build_decomp_block;
}
EXPORT_SYMBOL_GPL(adf_gen2_init_dc_ops);
