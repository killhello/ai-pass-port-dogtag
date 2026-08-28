#!/usr/bin/env python3
"""电子狗牌配对工具 - 通过BLE写入主人信息"""
import asyncio
import sys

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    print("请先安装 bleak: pip install bleak")
    sys.exit(1)

# GATT UUID（与固件一致）
SVC_UUID  = "f0debc9a-7856-3412-1234-567812345678"
NAME_UUID = "f1debc9a-7856-3412-1234-567812345678"
PHONE_UUID= "f2debc9a-7856-3412-1234-567812345678"
CMD_UUID  = "f4debc9a-7856-3412-1234-567812345678"
CMD_PAIR  = bytes([0x01])

async def main():
    name = input("请输入主人姓名: ").strip()
    phone = input("请输入联系电话: ").strip()
    if not name or not phone:
        print("姓名和电话不能为空")
        return

    print("正在搜索 DogTag 设备...")
    device = await BleakScanner.find_device_by_name("DogTag", timeout=10)
    if not device:
        print("未找到 DogTag 设备，请确认设备已开机")
        return

    print(f"找到设备: {device.name} ({device.address})")
    async with BleakClient(device) as client:
        print("已连接，写入信息...")
        await client.write_gatt_char(NAME_UUID, name.encode("utf-8"))
        await client.write_gatt_char(PHONE_UUID, phone.encode("utf-8"))
        await client.write_gatt_char(CMD_UUID, CMD_PAIR)
        print(f"配对完成！\n  姓名: {name}\n  电话: {phone}")

if __name__ == "__main__":
    asyncio.run(main())
