// Restricted Atmosphere type-0 main-data writes. Code/rodata/pointer cheats are rejected.
static NSString *const NBCheatsKey=@"NativeBridge.enabledCheats";
static uint8_t *nb_cheat_main;
static uint64_t nb_cheat_data,nb_cheat_size;
void nb_cheats_register(void *base,uint64_t data,uint64_t size) { nb_cheat_main=base; nb_cheat_data=data; nb_cheat_size=size; }
static NSArray *NBParseCheats(NSString *text,NSString **failure) {
    NSMutableArray *groups=[NSMutableArray array]; NSMutableDictionary *group=nil;
    for(NSString *raw in [text componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]) {
        NSString *line=[raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        if(!line.length || [line hasPrefix:@"#"] || [line hasPrefix:@"//"]) continue;
        if([line hasPrefix:@"["] && [line hasSuffix:@"]"]) {
            group=[@{@"name":[line substringWithRange:NSMakeRange(1,line.length-2)],@"writes":[NSMutableArray array],@"enabled":@NO} mutableCopy]; [groups addObject:group]; continue;
        }
        if(!group) { *failure=@"Codes must be inside [Cheat name] sections. Master-code sections are unsupported."; return nil; }
        NSArray *parts=[[line componentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceCharacterSet] filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"length > 0"]];
        if(parts.count<3 || parts.count>4) { *failure=@"Only static main-data writes are supported. Pointer, conditional and button codes need an interpreter not included here."; return nil; }
        uint32_t words[4]={0};
        for(NSUInteger i=0;i<parts.count;i++) {
            NSString *part=parts[i]; unsigned value=0; NSScanner *scanner=[NSScanner scannerWithString:part];
            if(part.length!=8 || ![scanner scanHexInt:&value] || !scanner.isAtEnd) { *failure=@"Each code word must contain exactly eight hexadecimal digits."; return nil; }
            words[i]=value;
        }
        unsigned width=(words[0]>>24)&15;
        if((words[0]&0xf0ffff00u)!=0 || ![@[@1,@2,@4,@8] containsObject:@(width)] || parts.count!=(width==8 ? 4 : 3)) { *failure=@"This file uses unsupported code types or addressing. Only type-0, main-relative writes with register zero are supported."; return nil; }
        uint64_t offset=((uint64_t)(words[0]&255)<<32)|words[1];
        uint64_t value=width==8 ? ((uint64_t)words[2]<<32)|words[3] : words[2];
        [group[@"writes"] addObject:@{@"offset":@(offset),@"width":@(width),@"value":@(value)}];
        if([group[@"writes"] count]>4096 || groups.count>128) { *failure=@"Too many cheat instructions."; return nil; }
    }
    if(!groups.count) { *failure=@"No cheat sections found."; return nil; }
    for(NSDictionary *entry in groups) if(![entry[@"writes"] count]) { *failure=@"An empty cheat section was found."; return nil; }
    return groups;
}
static NSString *NBCheckCheatWrites(NSArray *groups) {
    if(!nb_cheat_main) return @"Start the game before importing cheats so its data boundaries can be checked.";
    for(NSDictionary *group in groups) for(NSDictionary *write in group[@"writes"]) {
        uint64_t offset=[write[@"offset"] unsignedLongLongValue],width=[write[@"width"] unsignedLongLongValue];
        if(offset<nb_cheat_data || offset>nb_cheat_size || width>nb_cheat_size-offset) return @"This cheat patches executable/read-only memory or points outside the main module. It cannot run as an imported iPhone cheat; it needs a compatible build-time patch.";
    }
    return nil;
}
static NSString *NBCheatBuildID(void) {
    return [[NSString stringWithContentsOfFile:[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"nativebridge-build-id.txt"] encoding:NSUTF8StringEncoding error:nil] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
}
static void NBCheatTick(void) {
    NSString *current=NBCheatBuildID(), *stored=[NSUserDefaults.standardUserDefaults stringForKey:@"NativeBridge.cheatBuildID"];
    if(current.length!=16 || ![current isEqual:stored]) return;
    NSArray *groups=[NSUserDefaults.standardUserDefaults arrayForKey:NBCheatsKey];
    if(NBCheckCheatWrites(groups)) return;
    for(NSDictionary *group in groups) if([group[@"enabled"] boolValue]) for(NSDictionary *write in group[@"writes"]) {
        uint64_t offset=[write[@"offset"] unsignedLongLongValue],value=[write[@"value"] unsignedLongLongValue];
        size_t width=[write[@"width"] unsignedIntegerValue];
        if(width==1 || width==2 || width==4 || width==8) memcpy(nb_cheat_main+offset,&value,width);
    }
}
