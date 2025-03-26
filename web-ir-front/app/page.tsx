"use client";

import Image from "next/image";
import { useEffect, useRef, useState } from "react";
import { bluetooth } from "webbluetooth";

const SERVICE_ID = "19b10000-e8f2-537e-4f6c-d104768a1214";
const Data_Characteristic_UUIDs = [
  "19b10001-e8f2-537e-4f6c-d104768a1214",
  "19b10002-e8f2-537e-4f6c-d104768a1214",
  "19b10003-e8f2-537e-4f6c-d104768a1214",
  "19b10004-e8f2-537e-4f6c-d104768a1214",
];
const Meta_Characteristic_UUIDs = [
  "19b10005-e8f2-537e-4f6c-d104768a1214",
  "19b10006-e8f2-537e-4f6c-d104768a1214",
];

export default function Home() {
  const dataCharacteristics = useRef<BluetoothRemoteGATTCharacteristic[]>([]);
  const metaCharacteristics = useRef<BluetoothRemoteGATTCharacteristic[]>([]);
  const [length, setLength] = useState(-1);
  const [data, setData] = useState<number[]>([]);
  const [dataToSend, setDataToSend] = useState("");
  const [code, setCode] = useState("");
  const isReadingRef = useRef(false);

  const getDevice = async () => {
    const device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE_ID] }],
    });
    if (!device.gatt) return;
    const server = await device.gatt.connect();
    const service = await server.getPrimaryService(SERVICE_ID);
    const dcs = [];
    for (const uuid of Data_Characteristic_UUIDs) {
      const dc = await service.getCharacteristic(uuid);
      dcs.push(dc);
    }
    dataCharacteristics.current = dcs;
    const mcs = [];
    for (const uuid of Meta_Characteristic_UUIDs) {
      const mc = await service.getCharacteristic(uuid);
      mcs.push(mc);
    }
    metaCharacteristics.current = mcs;
    await mcs[0].addEventListener("characteristicvaluechanged", (e) =>
      readValue()
    );
    mcs[0].startNotifications();
    console.log("Connected");
  };

  const readValue = async () => {
    if (isReadingRef.current) return; // prevent overlapping reads
    isReadingRef.current = true;
    if (!metaCharacteristics.current) return;
    const length = (await metaCharacteristics.current[0].readValue()).getInt16(
      0,
      true
    );
    const rawCode = await metaCharacteristics.current[1].readValue();
    setCode(new TextDecoder().decode(rawCode));
    const decoded = [];
    for (let i = 0; i < dataCharacteristics.current.length; i++) {
      const value = await dataCharacteristics.current[i].readValue();
      for (let j = 0; j < value.byteLength; j += 2) {
        const index = i * 256 + j / 2;
        if (index >= length) break;
        decoded.push(value.getUint16(j, true));
      }
    }
    setLength(length);
    setData(decoded);
    isReadingRef.current = false;
  };

  const sendValue = async () => {
    if (!metaCharacteristics.current) return;
    const data = JSON.parse(dataToSend);
    const len = data.length;
    console.log(data);

    //uint16 to 4 characteristic up to 512 byte
    for (let i = 0; i < 4; i++) {
      const d = [];
      for (let j = 0; j < 256; j++) {
        let value = 0;
        if (i * 256 + j < len) {
          value = data[i * 256 + j];
        }
        d.push(value >> 8, value & 0xff);
      }
      await dataCharacteristics.current[i].writeValue(new Uint8Array(d));
      console.log(d);
    }
    const encoder = new TextEncoder();
    await metaCharacteristics.current[0].writeValue(
      encoder.encode(len.toString())
    );
    console.log("value written");
  };

  return (
    <div>
      <h1>Web IR</h1>
      <button onClick={() => getDevice()}>Find device</button>
      <button onClick={() => readValue()}>Read value</button>
      <button onClick={() => sendValue()}>Send value</button>
      <p>Length: {length}</p>
      <p>Code: {code}</p>
      <p>Data: {JSON.stringify(data)}</p>
      <input
        type="text"
        onChange={(e) => setDataToSend(e.target.value)}
        placeholder="Data to send"
      />
    </div>
  );
}
