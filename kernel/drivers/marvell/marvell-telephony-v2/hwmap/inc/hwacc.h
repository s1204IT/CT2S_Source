
#ifndef HWACC_H
#define HWACC_H

#define CO_PRO_OPERAND_OPC1_MASK		0x7
#define CO_PRO_OPERAND_CRN_MASK			0xf
#define CO_PRO_OPERAND_RT_MASK			0xf
#define CO_PRO_OPERAND_COPROC_MASK		0xf
#define CO_PRO_OPERAND_OPC2_MASK		0x7
#define CO_PRO_OPERAND_CRM_MASK			0xf

#define CO_PRO_OPERAND_OPC1_OFSET		21
#define CO_PRO_OPERAND_CRN_OFSET		16
#define CO_PRO_OPERAND_RT_OFSET			12
#define CO_PRO_OPERAND_COPROC_OFSET		8
#define CO_PRO_OPERAND_OPC2_OFSET		5
#define CO_PRO_OPERAND_CRM_OFSET		0

#define CO_PRO_OPERAND_OPC1_SET(x)		((x & CO_PRO_OPERAND_OPC1_MASK) << CO_PRO_OPERAND_OPC1_OFSET)
#define CO_PRO_OPERAND_CRN_SET(x) 		((x & CO_PRO_OPERAND_CRN_MASK) << CO_PRO_OPERAND_CRN_OFSET)
#define CO_PRO_OPERAND_RT_SET(x) 		((x & CO_PRO_OPERAND_RT_MASK) << CO_PRO_OPERAND_RT_OFSET)
#define CO_PRO_OPERAND_COPROC_SET(x) 	((x & CO_PRO_OPERAND_COPROC_MASK) << CO_PRO_OPERAND_COPROC_OFSET)
#define CO_PRO_OPERAND_OPC2_SET(x) 		((x & CO_PRO_OPERAND_OPC2_MASK) << CO_PRO_OPERAND_OPC2_OFSET)
#define CO_PRO_OPERAND_CRM_SET(x) 		((x & CO_PRO_OPERAND_CRM_MASK))

#define ARM_V7_MCR_MASK					0xee000010
#define ARM_V7_MRC_MASK					0xee100010

#define HWAP_IOCTL_MRC					0
#define HWAP_IOCTL_MCR					1
#define HWAP_IOCTL_TEST					3

/*ARM co-proc op code operands for MRC and MCR - From MSB to LSB*/
struct cp_operands {
	unsigned int opc1;
	unsigned int crn;
	unsigned long rt;
	unsigned int coproc;
	unsigned int opc2;
	unsigned int crm;
};

#endif
