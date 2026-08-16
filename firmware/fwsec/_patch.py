import io
p='fwsec_container.cpp'
s=io.open(p,encoding='utf-8').read()
old='''    const u32 bs = info.block_size;
    u64 mac_remaining = info.total_size;
    for (u64 i = 0; i < info.blocks; i++) {
        size_t n = (size_t)(mac_remaining > bs ? bs : mac_remaining);'''
new='''    const u32 mac_bs = info.block_size;
    u64 mac_remaining = info.total_size;
    for (u64 i = 0; i < info.blocks; i++) {
        size_t n = (size_t)(mac_remaining > mac_bs ? mac_bs : mac_remaining);'''
assert old in s
s=s.replace(old,new)
io.open(p,'w',encoding='utf-8').write(s)
print('patched ok')
