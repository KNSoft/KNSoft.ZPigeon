const ADAPTER=35,ADDRESS=36,TCP=38;
const IPV4=2,IPV6=23;

const adapterType=value=>({
  6:'以太网',9:'令牌环',23:'PPP',24:'环回',37:'ATM',53:'虚拟接口',62:'Fast Ethernet',
  69:'Fast Ethernet FX',71:'Wi-Fi',94:'ADSL',117:'千兆以太网',131:'隧道',
  144:'IEEE 1394',243:'移动宽带 GSM',244:'移动宽带 CDMA'
})[value]||`类型 ${value}`;

const operStatus=value=>['','已连接','未连接','测试中','未知','休眠','不存在','下层未连接'][value]||
  `状态 ${value}`;
const familyName=value=>value===IPV4?'IPv4':value===IPV6?'IPv6':`地址族 ${value}`;
const formatBits=value=>{
  const number=Number(value);
  if(!number)return'—';
  const units=['bps','Kbps','Mbps','Gbps','Tbps'];
  let index=0,result=number;
  while(result>=1000&&index<units.length-1){
    result/=1000;
    index++;
  }
  return`${result>=100?result.toFixed(0):result>=10?result.toFixed(1):result.toFixed(2)} ${units[index]}`;
};

const formatBytes=value=>{
  const number=Number(value);
  if(!number)return'0 B';
  const units=['B','KB','MB','GB','TB'];
  let index=0,result=number;
  while(result>=1024&&index<units.length-1){
    result/=1024;
    index++;
  }
  return`${result>=100?result.toFixed(0):result>=10?result.toFixed(1):result.toFixed(2)} ${units[index]}`;
};

class NetworkSnapshotManager{
  constructor(root,{call,notify,endpoint}){
    this.root=root;
    this.call=call;
    this.notify=notify;
    this.endpoint=endpoint;
    this.connected=false;
  }

  activate(connected){
    this.connected=connected;
    if(connected&&!this.loaded)this.load();
    else this.render();
  }

  disconnect(){
    this.connected=false;
    this.loaded=false;
    this.records=[];
    this.render();
  }

  async load(){
    if(!this.connected||this.loading)return;
    this.loading=true;
    this.loaded=false;
    this.records=[];
    this.render();
    this.empty.textContent=this.loadingText;
    try{
      this.records=await this.call(this.endpoint);
      this.loaded=true;
      this.render();
    }catch(error){
      this.render();
      this.empty.textContent=error.message;
      this.empty.hidden=false;
      this.notify(error);
    }finally{
      this.loading=false;
    }
  }

  visible(records=this.records||[]){
    const query=this.filter.value.toLocaleLowerCase();
    return records.filter(record=>!query||Object.values(record).some(value=>
      String(value).toLocaleLowerCase().includes(query)));
  }

  finishRender(records,total){
    this.summary.textContent=this.loaded?`${total} 项`:'';
    this.empty.hidden=records.length!==0;
    if(!this.connected)this.empty.textContent='Client 未连接';
    else if(!this.loaded)this.empty.textContent='尚未读取';
    else if(!records.length)this.empty.textContent=total?'没有匹配的项目':'没有数据';
    this.refresh.disabled=!this.connected;
  }
}

export class NetworkAdapterManager extends NetworkSnapshotManager{
  constructor(root,{call,notify}){
    super(root,{call,notify,endpoint:'/api/network-adapters'});
    this.loadingText='正在读取网络适配器…';
    root.innerHTML=`
      <div class="manager-toolbar">
        <input data-role="filter" placeholder="筛选网络适配器">
        <label><input type="checkbox" data-role="hidden">显示隐藏适配器</label>
        <span class="status" data-role="summary"></span><span class="spacer"></span>
        <button data-action="refresh">刷新</button>
      </div>
      <div class="manager-table"><table>
        <thead><tr><th>名称</th><th>状态</th><th>网络配置文件</th><th>类型</th><th>IP 地址</th><th>速度</th><th>描述</th></tr></thead>
        <tbody></tbody></table><div class="manager-empty">Client 未连接</div>
      </div>
      <div class="context-menu administration-menu" data-role="menu" hidden></div>
      <dialog data-role="properties"><form method="dialog">
        <h2>网络适配器属性</h2><dl class="details-grid"></dl>
        <div class="dialog-actions"><button value="close">关闭</button></div>
      </form></dialog>`;
    this.filter=root.querySelector('[data-role=filter]');
    this.showHidden=root.querySelector('[data-role=hidden]');
    this.summary=root.querySelector('[data-role=summary]');
    this.refresh=root.querySelector('[data-action=refresh]');
    this.empty=root.querySelector('.manager-empty');
    this.body=root.querySelector('tbody');
    this.menu=root.querySelector('[data-role=menu]');
    this.dialog=root.querySelector('[data-role=properties]');
    this.filter.oninput=()=>this.render();
    this.showHidden.onchange=()=>this.render();
    this.refresh.onclick=()=>this.load();
    this.body.ondblclick=event=>{
      const row=event.target.closest('tr');
      if(row)this.properties(row.record);
    };
    this.body.oncontextmenu=event=>{
      const row=event.target.closest('tr');
      if(row){
        event.preventDefault();
        this.openMenu(event,row.record);
      }
    };
    addEventListener('pointerdown',event=>{
      if(!this.menu.contains(event.target))this.menu.hidden=true;
    });
    this.render();
  }

  disconnect(){
    super.disconnect();
    this.menu.hidden=true;
    if(this.dialog.open)this.dialog.close();
  }

  addresses(identity){
    return(this.records||[]).filter(record=>record.kind===ADDRESS&&record.identity===identity);
  }

  render(){
    const all=(this.records||[]).filter(record=>record.kind===ADAPTER),visible=all.filter(record=>this.showHidden.checked||!(record.state&0x80000000));
    const records=this.visible(visible).sort((left,right)=>left.name.localeCompare(right.name));
    this.body.replaceChildren(...records.map(record=>this.row(record)));
    this.finishRender(records,visible.length);
  }

  row(record){
    const row=document.createElement('tr'),admin=(record.state>>>16)&0xffff,addresses=this.addresses(record.identity),category=Number((record.detail||'').split('\n')[10]);
    const values=[record.name,admin===2?'已禁用':operStatus(record.state&0xffff),category===0?'公用网络':category===1?'专用网络':category===2?'域网络':'—',adapterType(record.flags),
                  addresses.map(address=>`${address.name}/${address.value}`).join(', '),
                  formatBits(record.value),record.description];
    row.record=record;
    for(const value of values){
      const cell=row.insertCell();
      cell.textContent=value||'—';
      cell.title=cell.textContent;
    }
    return row;
  }

  openMenu(event,record){
    const disabled=((record.state>>>16)&0xffff)===2;
    const category=Number((record.detail||'').split('\n')[10]),actions=[[disabled?'启用':'禁用',()=>this.setEnabled(record,disabled)],
                   ['设为公用网络',()=>this.setCategory(record,0),false,category===0||category===2||!Number.isInteger(category)],
                   ['设为专用网络',()=>this.setCategory(record,1),false,category===1||category===2||!Number.isInteger(category)],
                   ['属性',()=>this.properties(record)]];
    this.menu.replaceChildren(...actions.map(([name,action,danger,actionDisabled])=>{
      const button=document.createElement('button');
      button.textContent=name;
      button.classList.toggle('danger',!disabled&&name==='禁用');
      button.disabled=actionDisabled===true;
      button.onclick=()=>{
        this.menu.hidden=true;
        action();
      };
      return button;
    }));
    this.menu.hidden=false;
    const box=this.menu.getBoundingClientRect();
    this.menu.style.left=`${Math.max(6,Math.min(event.clientX,innerWidth-box.width-6))}px`;
    this.menu.style.top=`${Math.max(6,Math.min(event.clientY,innerHeight-box.height-6))}px`;
  }

  async setEnabled(record,enabled){
    if(!enabled&&!confirm(`禁用“${record.name}”可能立即中断当前控制连接，确定继续？`))return;
    try{
      await this.call('/api/network-adapters/control',{action:enabled?3:4,identity:record.identity});
      this.notify(enabled?'网络适配器已启用':'网络适配器已禁用');
      await this.load();
    }catch(error){
      this.notify(error);
    }
  }

  async setCategory(record,category){try{await this.call('/api/network-adapters/control',{action:23,identity:record.identity,argument:String(category)});this.notify(category?'已设为专用网络':'已设为公用网络');await this.load()}catch(error){this.notify(error)}}

  properties(record){
    const detail=(record.detail||'').split('\n'),addresses=this.addresses(record.identity);
    const ipv4=addresses.filter(item=>item.state===IPV4).map(item=>`${item.name}/${item.value}`).join('\n')||'—';
    const ipv6=addresses.filter(item=>item.state===IPV6).map(item=>`${item.name}/${item.value}`).join('\n')||'—';
    const fields=[['名称',record.name],['描述',record.description],['接口索引',record.identity],
                  ['管理状态',((record.state>>>16)&0xffff)===2?'已禁用':'已启用'],
                  ['运行状态',operStatus(record.state&0xffff)],['类型',adapterType(record.flags)],
                  ['MAC 地址',detail[0]||'—'],['MTU',detail[1]||'—'],['发送速度',formatBits(record.value)],
                  ['接收速度',formatBits(detail[2])],['接收流量',formatBytes(detail[3])],
                  ['发送流量',formatBytes(detail[4])],['接收错误',detail[5]||'0'],
                  ['发送错误',detail[6]||'0'],['发送队列',detail[7]||'0'],['IPv4',ipv4],['IPv6',ipv6]];
    fields.splice(6,0,['网络配置文件',Number(detail[10])===0?'公用网络':Number(detail[10])===1?'专用网络':Number(detail[10])===2?'域网络':'—']);
    this.dialog.querySelector('dl').replaceChildren(...fields.flatMap(([name,value])=>{
      const term=document.createElement('dt'),description=document.createElement('dd');
      term.textContent=name;
      description.textContent=value||'—';
      return[term,description];
    }));
    this.dialog.showModal();
  }
}

const routeProtocol=value=>({
  1:'其它',2:'本地',3:'网络管理',4:'ICMP',8:'RIP',9:'IS-IS',13:'OSPF',14:'BGP',19:'DHCP',
  10002:'自动静态',10006:'静态',10007:'静态（非按需）'
})[value]||String(value);
const routeOrigin=value=>['手动','已知路由','DHCP','路由器通告','6to4'][value]||String(value);

export class NetworkRouteManager extends NetworkSnapshotManager{
  constructor(root,{call,notify}){
    super(root,{call,notify,endpoint:'/api/network-routes'});
    this.loadingText='正在读取路由表…';
    root.innerHTML=`
      <div class="manager-toolbar">
        <input data-role="filter" placeholder="筛选路由">
        <span class="status" data-role="summary"></span><span class="spacer"></span>
        <button data-action="new">新建</button>
        <button data-action="refresh">刷新</button>
      </div>
      <div class="manager-table"><table>
        <thead><tr><th>目标</th><th>下一跃点</th><th>接口</th><th>索引</th><th>地址族</th>
          <th>跃点数</th><th>协议</th><th>来源</th></tr></thead><tbody></tbody>
        </table><div class="manager-empty">Client 未连接</div>
      </div>
      <div class="context-menu administration-menu" data-role="menu" hidden><button data-action="edit">修改跃点数</button><button data-action="delete" class="danger">删除</button></div>
      <dialog data-role="route"><form><h2 data-role="title"></h2><label>地址族<select data-field="family"><option value="2">IPv4</option><option value="23">IPv6</option></select></label><label>目标前缀<input data-field="destination" required placeholder="0.0.0.0/0"></label><label>下一跃点<input data-field="next-hop" required placeholder="0.0.0.0"></label><label>接口索引<input data-field="interface" type="number" min="1" required></label><label>跃点数<input data-field="metric" type="number" min="0" required></label><div class="dialog-actions"><button type="submit">保存</button><button type="button" data-action="cancel">取消</button></div></form></dialog>`;
    this.filter=root.querySelector('[data-role=filter]');
    this.summary=root.querySelector('[data-role=summary]');
    this.refresh=root.querySelector('[data-action=refresh]');
    this.empty=root.querySelector('.manager-empty');
    this.body=root.querySelector('tbody');
    this.menu=root.querySelector('[data-role=menu]');
    this.dialog=root.querySelector('[data-role=route]');
    this.filter.oninput=()=>this.render();
    this.refresh.onclick=()=>this.load();
    root.querySelector('[data-action=new]').onclick=()=>this.edit();
    this.dialog.querySelector('[data-action=cancel]').onclick=()=>this.dialog.close();
    this.dialog.querySelector('form').onsubmit=event=>{event.preventDefault();this.save()};
    this.body.oncontextmenu=event=>{const row=event.target.closest('tr');if(!row)return;event.preventDefault();this.selected=row.record;this.menu.hidden=false;const box=this.menu.getBoundingClientRect();this.menu.style.left=`${Math.max(6,Math.min(event.clientX,innerWidth-box.width-6))}px`;this.menu.style.top=`${Math.max(6,Math.min(event.clientY,innerHeight-box.height-6))}px`};
    this.menu.querySelector('[data-action=edit]').onclick=()=>{this.menu.hidden=true;this.edit(this.selected)};
    this.menu.querySelector('[data-action=delete]').onclick=()=>{this.menu.hidden=true;this.remove(this.selected)};
    addEventListener('pointerdown',event=>{if(!this.menu.contains(event.target))this.menu.hidden=true});
    this.render();
  }

  render(){
    const all=this.records||[];
    const records=this.visible().sort((left,right)=>left.state-right.state||left.name.localeCompare(right.name));
    this.body.replaceChildren(...records.map(record=>{
      const row=document.createElement('tr'),protocol=record.flags&0xffff,origin=record.flags>>>16,index=record.identity.split('|')[1]||'';
      row.record=record;const values=[record.name,record.description,record.detail||'—',index,familyName(record.state),
                    record.value,routeProtocol(protocol),routeOrigin(origin)];
      for(const value of values){
        const cell=row.insertCell();
        cell.textContent=value;
        cell.title=cell.textContent;
      }
      return row;
    }));
    this.finishRender(records,all.length);
  }

  edit(record=null){this.editing=record;const field=name=>this.dialog.querySelector(`[data-field="${name}"]`),parts=record?.identity.split('|');this.dialog.querySelector('[data-role=title]').textContent=record?'修改路由':'新建路由';field('family').value=String(record?.state||2);field('destination').value=record?.name||(record?.state===IPV6?'::/0':'0.0.0.0/0');field('next-hop').value=record?.description||(record?.state===IPV6?'::':'0.0.0.0');field('interface').value=parts?.[1]||'';field('metric').value=record?.value||0;for(const name of ['family','destination','next-hop','interface'])field(name).disabled=!!record;this.dialog.showModal();field(record?'metric':'destination').focus()}

  async save(){const field=name=>this.dialog.querySelector(`[data-field="${name}"]`).value,identity=this.editing?.identity||`${field('family')}|${field('interface')}|${field('destination')}|${field('next-hop')}`;try{await this.call('/api/network-routes/control',{action:this.editing?23:1,identity,argument:field('metric')});this.dialog.close();this.notify('路由已保存');await this.load()}catch(error){this.notify(error)}}

  async remove(record){if(!confirm(`确定删除路由 ${record.name}？`))return;try{await this.call('/api/network-routes/control',{action:2,identity:record.identity});this.notify('路由已删除');await this.load()}catch(error){this.notify(error)}}
}

const tcpState=value=>({
  1:'已关闭',2:'侦听',3:'SYN 已发送',4:'SYN 已接收',5:'已建立',6:'FIN 等待 1',7:'FIN 等待 2',
  8:'关闭等待',9:'正在关闭',10:'最后确认',11:'时间等待',12:'删除 TCB'
})[value]||String(value);

export class NetworkEndpointManager extends NetworkSnapshotManager{
  constructor(root,{call,notify}){
    super(root,{call,notify,endpoint:'/api/network-endpoints'});
    this.loadingText='正在读取网络连接…';
    root.innerHTML=`
      <div class="manager-toolbar">
        <input data-role="filter" placeholder="筛选地址或 PID">
        <select data-role="protocol">
          <option value="all">全部</option><option value="tcp">TCP</option><option value="udp">UDP</option>
          <option value="ipv4">IPv4</option><option value="ipv6">IPv6</option>
        </select>
        <span class="status" data-role="summary"></span><span class="spacer"></span>
        <button data-action="refresh">刷新</button>
      </div>
      <div class="manager-table"><table>
        <thead><tr><th>协议</th><th>本地地址</th><th>远程地址</th><th>状态</th><th>PID</th></tr></thead>
        <tbody></tbody></table><div class="manager-empty">Client 未连接</div>
      </div>`;
    this.filter=root.querySelector('[data-role=filter]');
    this.protocol=root.querySelector('[data-role=protocol]');
    this.summary=root.querySelector('[data-role=summary]');
    this.refresh=root.querySelector('[data-action=refresh]');
    this.empty=root.querySelector('.manager-empty');
    this.body=root.querySelector('tbody');
    this.filter.oninput=this.protocol.onchange=()=>this.render();
    this.refresh.onclick=()=>this.load();
    this.render();
  }

  render(){
    const all=this.records||[],selection=this.protocol.value;
    const records=this.visible().filter(record=>selection==='all'||
      selection==='tcp'&&record.kind===TCP||selection==='udp'&&record.kind!==TCP||
      selection==='ipv4'&&record.flags===IPV4||selection==='ipv6'&&record.flags===IPV6);
    records.sort((left,right)=>left.kind-right.kind||left.name.localeCompare(right.name));
    this.body.replaceChildren(...records.map(record=>{
      const tcp=record.kind===TCP,row=document.createElement('tr');
      const values=[`${tcp?'TCP':'UDP'}${record.flags===IPV4?'v4':'v6'}`,
                    record.name,record.description||'—',tcp?tcpState(record.state):'—',record.value];
      for(const value of values){
        const cell=row.insertCell();
        cell.textContent=value;
        cell.title=cell.textContent;
      }
      return row;
    }));
    this.finishRender(records,all.length);
  }
}
