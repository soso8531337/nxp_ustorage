/*
 * @note
 * Copyright(C) i4season U-Storage, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 */


#ifndef __USUSB_H_
#define __USUSB_H_

#include <ctype.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \ingroup desc
 * A structure representing the standard USB device descriptor. This
 * descriptor is documented in section 9.6.1 of the USB 2.0 specification.
 * All multiple-byte fields are represented in host-endian format.
 */
typedef struct  {
	/** Size of this descriptor (in bytes) */
	uint8_t  bLength;
	/** Descriptor type. Will have value
	 * \ref libusb_descriptor_type::LIBUSB_DT_DEVICE LIBUSB_DT_DEVICE in this
	 * context. */
	uint8_t  bDescriptorType;
	/** USB specification release number in binary-coded decimal. A value of
	 * 0x0200 indicates USB 2.0, 0x0110 indicates USB 1.1, etc. */
	uint16_t bcdUSB;
	/** USB-IF class code for the device. See \ref libusb_class_code. */
	uint8_t  bDeviceClass;
	/** USB-IF subclass code for the device, qualified by the bDeviceClass
	 * value */
	uint8_t  bDeviceSubClass;
	/** USB-IF protocol code for the device, qualified by the bDeviceClass and
	 * bDeviceSubClass values */
	uint8_t  bDeviceProtocol;
	/** Maximum packet size for endpoint 0 */
	uint8_t  bMaxPacketSize0;
	/** USB-IF vendor ID */
	uint16_t idVendor;
	/** USB-IF product ID */
	uint16_t idProduct;
	/** Device release number in binary-coded decimal */
	uint16_t bcdDevice;
	/** Index of string descriptor describing manufacturer */
	uint8_t  iManufacturer;
	/** Index of string descriptor describing product */
	uint8_t  iProduct;
	/** Index of string descriptor containing device serial number */
	uint8_t  iSerialNumber;
	/** Number of possible configurations */
	uint8_t  bNumConfigurations;
}USB_StdDesDevice_t;

/** \ingroup desc
 * A structure representing the standard USB interface descriptor. This
 * descriptor is documented in section 9.6.5 of the USB 2.0 specification.
 * All multiple-byte fields are represented in host-endian format.
 */
typedef struct  {
	/** Size of this descriptor (in bytes) */
	uint8_t  bLength;
	/** Descriptor type. Will have value
	 * \ref libusb_descriptor_type::LIBUSB_DT_INTERFACE LIBUSB_DT_INTERFACE
	 * in this context. */
	uint8_t  bDescriptorType;
	/** Number of this interface */
	uint8_t  bInterfaceNumber;
	/** Value used to select this alternate setting for this interface */
	uint8_t  bAlternateSetting;
	/** Number of endpoints used by this interface (excluding the control
	 * endpoint). */
	uint8_t  bNumEndpoints;
	/** USB-IF class code for this interface. See \ref libusb_class_code. */
	uint8_t  bInterfaceClass;
	/** USB-IF subclass code for this interface, qualified by the
	 * bInterfaceClass value */
	uint8_t  bInterfaceSubClass;
	/** USB-IF protocol code for this interface, qualified by the
	 * bInterfaceClass and bInterfaceSubClass values */
	uint8_t  bInterfaceProtocol;
	/** Index of string descriptor describing this interface */
	uint8_t  iInterface;
}USB_StdDesInterface_t;

/** \ingroup desc
 * A structure representing the standard USB endpoint descriptor. This
 * descriptor is documented in section 9.6.3 of the USB 2.0 specification.
 * All multiple-byte fields are represented in host-endian format.
 */
typedef struct _ USB_StdDesEndpoint_t{
	/** Size of this descriptor (in bytes) */
	uint8_t  bLength;
	/** Descriptor type. Will have value
	 * \ref libusb_descriptor_type::LIBUSB_DT_ENDPOINT LIBUSB_DT_ENDPOINT in
	 * this context. */
	uint8_t  bDescriptorType;
	/** The address of the endpoint described by this descriptor. Bits 0:3 are
	 * the endpoint number. Bits 4:6 are reserved. Bit 7 indicates direction,
	 * see \ref libusb_endpoint_direction.
	 */
	uint8_t  bEndpointAddress;
	/** Attributes which apply to the endpoint when it is configured using
	 * the bConfigurationValue. Bits 0:1 determine the transfer type and
	 * correspond to \ref libusb_transfer_type. Bits 2:3 are only used for
	 * isochronous endpoints and correspond to \ref libusb_iso_sync_type.
	 * Bits 4:5 are also only used for isochronous endpoints and correspond to
	 * \ref libusb_iso_usage_type. Bits 6:7 are reserved.
	 */
	uint8_t  bmAttributes;
	/** Maximum packet size this endpoint is capable of sending/receiving. */
	uint16_t wMaxPacketSize;
	/** Interval for polling endpoint for data transfers. */
	uint8_t  bInterval;
}USB_StdDesEndpoint_t;




/*NXP LPC18XX usb relative struct*/
typedef uint8_t (*usbBulkCallback)(void *);
typedef uint8_t (* ConfigComparator)(void*);
typedef struct {
	uint8_t usbnum; /*USB Number*/
	uint8_t endnum;/*USB Endpoint Number*/
	uint16_t packetsize; /*Max Packet Size*/
	usbBulkCallback callback;	/*Callback*/
	void *data;/*Callback parameter*/
}nxp_bluk;

typedef struct{
	uint8_t usbnum; /*USB Number*/
	uint8_t cfgindex;/*config index*/	
	uint8_t cfgnum;/*config Number*/
}nxp_desconfig;

typedef struct {
	void *InterfaceInfo;
	void *ConfigDescriptorData;
	uint16_t ConfigDescriptorSize;
	ConfigComparator *callbackInterface;	/*Callback*/
	ConfigComparator *callbackEndpoint;	/*Callback*/
}nxp_clminface;


uint8_t usUsb_SendControlRequest(void *cPrivate, 
			uint8_t bmRequestType, uint8_t bRequest, 
			uint16_t wValue, uint16_t wIndex, uint16_t wLength, const void *data);
uint8_t usUsb_BlukPacketSend(void *cPrivate, uint8_t *buffer, const uint32_t length);
uint8_t usUsb_BlukPacketReceive(void *cPrivate, uint8_t *buffer, uint32_t length);
uint8_t usUusb_GetDeviceDescriptor(void *cPrivate, USB_StdDesDevice_t *DeviceDescriptorData);
uint8_t usUusb_GetDeviceConfigDescriptor(void *cPrivate, 
							uint8_t *ConfigDescriptorData, uint16_t ConfigDescriptorDataLen);

uint8_t usUusb_SetDeviceConfigDescriptor(void *cPrivate);
uint8_t usUusb_ClaimInterface(void *cPrivate);


#ifdef __cplusplus
}
#endif

