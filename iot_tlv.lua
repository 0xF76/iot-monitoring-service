local p_iot = Proto("iot_tlv", "IoT TLV Protocol")

local f_type  = ProtoField.uint16("iot_tlv.type",   "Type",   base.HEX)
local f_len   = ProtoField.uint16("iot_tlv.length", "Length", base.DEC)
local f_value = ProtoField.bytes ("iot_tlv.value",  "Value")

p_iot.fields = { f_type, f_len, f_value }

local TLV_NAMES = {
  [0x0001] = "DISCOVER_REQUEST",
  [0x0002] = "DISCOVER_RESPONSE",
  [0x0010] = "LIST_REQUEST",
  [0x0011] = "LIST_RESPONSE",
  [0x0013] = "GET_REQUEST",
  [0x0014] = "GET_RESPONSE",
  [0x0015] = "SET_REQUEST",
  [0x0016] = "SET_RESPONSE",
}

function p_iot.dissector(tvbuf, pinfo, tree)
  pinfo.cols.protocol = "IOT_TLV"

  local offset = 0
  local total = tvbuf:len()

  while offset < total do
    -- Need header (type+len)
    if (total - offset) < 4 then
      pinfo.desegment_offset = offset
      pinfo.desegment_len = 4 - (total - offset)
      return
    end

    local t = tvbuf(offset, 2):uint()
    local l = tvbuf(offset + 2, 2):uint()
    local msg_len = 4 + l

    -- Need full TLV
    if (total - offset) < msg_len then
      pinfo.desegment_offset = offset
      pinfo.desegment_len = msg_len - (total - offset)
      return
    end

    local name = TLV_NAMES[t] or "UNKNOWN"

    local subtree = tree:add(
      p_iot,
      tvbuf(offset, msg_len),
      string.format("TLV %s (0x%04X), length=%d", name, t, l)
    )

    subtree:add(f_type, tvbuf(offset, 2)):append_text(" (" .. name .. ")")
    subtree:add(f_len, tvbuf(offset + 2, 2))

    if l > 0 then
      subtree:add(f_value, tvbuf(offset + 4, l))
    end

    offset = offset + msg_len
  end
end

DissectorTable.get("tcp.port"):add(5001, p_iot)
DissectorTable.get("udp.port"):add(5000, p_iot)
