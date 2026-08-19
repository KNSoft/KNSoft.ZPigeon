const PUBLISHED=33,UNLIMITED=0xffffffff;

const shareType=value=>{
  const type=value&0xff;
  const name=type===0?'文件夹':type===1?'打印机':type===2?'设备':type===3?'IPC':`类型 ${type}`;
  return`${name}${value&0x80000000?' · 特殊':''}${value&0x40000000?' · 临时':''}`;
};

const useType=value=>value===0?'通配':value===1?'磁盘':value===2?'打印机':value===3?'IPC':`类型 ${value}`;
const useState=value=>['已连接','已暂停','会话丢失','已断开','网络错误','正在连接','正在重连'][value]||
  `状态 ${value}`;

export class NetworkShareManager{
  constructor(root,{call,notify,filePicker,aclEditor}){
    this.root=root;
    this.call=call;
    this.notify=notify;
    this.filePicker=filePicker;
    this.aclEditor=aclEditor;
    this.view='published';
    this.records=new Map;
    root.innerHTML=`
      <div class="manager-toolbar">
        <nav class="property-tabs network-share-tabs">
          <button data-view="published" class="active">共享出去</button>
          <button data-view="connections">连接的共享</button>
        </nav>
        <input data-role="filter" placeholder="筛选网络共享">
        <span class="status" data-role="summary"></span><span class="spacer"></span>
        <button data-action="new">新建共享</button><button data-action="refresh">刷新</button>
      </div>
      <div class="manager-table network-share-list">
        <table><thead></thead><tbody></tbody></table><div class="manager-empty">Client 未连接</div>
      </div>
      <div class="context-menu administration-menu" data-role="menu" hidden></div>
      <dialog data-role="published"><form>
        <h2></h2>
        <label>共享名称<input data-field="name" required autocomplete="off"></label>
        <label>本地文件夹<span class="remote-path-field">
          <input data-field="path" required spellcheck="false">
          <button type="button" data-action="browse">选择…</button>
        </span></label>
        <label>备注<input data-field="remark" autocomplete="off"></label>
        <label data-role="maximum">最大连接数
          <input data-field="maximum" type="number" min="1" max="4294967295" required>
        </label>
        <p class="property-note" data-role="published-note"></p>
        <div class="dialog-actions">
          <button type="button" data-action="cancel">取消</button><button type="submit">确定</button>
        </div>
      </form></dialog>
      <dialog data-role="connection"><form>
        <h2></h2>
        <label>远程共享路径
          <input data-field="remote" required placeholder="\\\\server\\share" spellcheck="false">
        </label>
        <label>本地设备（可选）
          <input data-field="local" placeholder="Z:" maxlength="2" spellcheck="false">
        </label>
        <label>用户名（可选）<input data-field="username" autocomplete="username"></label>
        <label>密码（可选）
          <input data-field="password" type="password" autocomplete="new-password">
        </label>
        <p class="property-note">
          连接属于 Client 的运行身份。修改现有连接会先断开，再按新设置重新连接。
        </p>
        <div class="dialog-actions">
          <button type="button" data-action="cancel">取消</button><button type="submit">连接</button>
        </div>
      </form></dialog>
      <dialog data-role="properties"><form method="dialog">
        <h2>网络共享属性</h2><dl class="details-grid"></dl>
        <div class="dialog-actions"><button value="close">关闭</button></div>
      </form></dialog>`;
    this.filter=root.querySelector('[data-role=filter]');
    this.head=root.querySelector('thead');
    this.body=root.querySelector('tbody');
    this.empty=root.querySelector('.manager-empty');
    this.summary=root.querySelector('[data-role=summary]');
    this.newButton=root.querySelector('[data-action=new]');
    this.menu=root.querySelector('[data-role=menu]');
    this.publishedDialog=root.querySelector('[data-role=published]');
    this.connectionDialog=root.querySelector('[data-role=connection]');
    this.propertiesDialog=root.querySelector('[data-role=properties]');
    for(const tab of root.querySelectorAll('[data-view]'))tab.onclick=()=>this.select(tab.dataset.view);
    this.filter.oninput=()=>this.render();
    this.newButton.onclick=()=>this.edit();
    root.querySelector('[data-action=refresh]').onclick=()=>this.load();
    this.body.oncontextmenu=event=>{
      const row=event.target.closest('tr');
      if(row){
        event.preventDefault();
        this.openMenu(event,row.record);
      }
    };
    this.body.ondblclick=event=>{
      const row=event.target.closest('tr');
      if(row)this.properties(row.record);
    };
    this.publishedDialog.querySelector('[data-action=cancel]').onclick=()=>this.publishedDialog.close();
    this.publishedDialog.querySelector('[data-action=browse]').onclick=()=>this.browse();
    this.publishedDialog.querySelector('form').onsubmit=event=>{
      event.preventDefault();
      this.savePublished();
    };
    this.connectionDialog.querySelector('[data-action=cancel]').onclick=()=>this.connectionDialog.close();
    this.connectionDialog.querySelector('form').onsubmit=event=>{
      event.preventDefault();
      this.saveConnection();
    };
    this.connectionDialog.onclose=()=>this.connectionDialog.querySelector('[data-field=password]').value='';
    addEventListener('pointerdown',event=>{
      if(!this.menu.contains(event.target))this.menu.hidden=true;
    });
    this.render();
  }

  activate(connected){
    this.connected=connected;
    if(connected&&!this.records.has(this.view))this.load();
    else this.render();
  }

  disconnect(){
    this.connected=false;
    this.loading=false;
    this.request=(this.request||0)+1;
    this.records.clear();
    this.menu.hidden=true;
    if(this.publishedDialog.open)this.publishedDialog.close();
    if(this.connectionDialog.open)this.connectionDialog.close();
    if(this.propertiesDialog.open)this.propertiesDialog.close();
    this.render();
  }

  select(view){
    this.view=view;
    for(const tab of this.root.querySelectorAll('[data-view]')){
      tab.classList.toggle('active',tab.dataset.view===view);
    }
    this.newButton.textContent=view==='published'?'新建共享':'新建连接';
    if(this.connected&&!this.records.has(view))this.load();
    else this.render();
  }

  endpoint(){
    return`/api/network-shares/${this.view}`;
  }

  async load(){
    if(!this.connected||this.loading)return;
    const view=this.view,request=(this.request||0)+1;
    this.request=request;
    this.loading=true;
    this.empty.hidden=false;
    this.empty.textContent='正在读取网络共享…';
    try{
      this.records.set(view,await this.call(this.endpoint()));
      if(request===this.request)this.render();
    }catch(error){
      if(request!==this.request)return;
      this.records.delete(view);
      this.render();
      this.empty.textContent=error.message;
      this.notify(error);
    }finally{
      if(request===this.request){
        this.loading=false;
        if(this.connected&&!this.records.has(this.view))this.load();
      }
    }
  }

  render(){
    const loaded=this.records.has(this.view),query=this.filter.value.toLocaleLowerCase();
    const all=this.records.get(this.view)||[];
    const records=all.filter(record=>!query||Object.values(record).some(value=>
      String(value).toLocaleLowerCase().includes(query)));
    const columns=this.view==='published'?
      ['共享名称','本地路径','类型','当前连接','最大连接','备注']:
      ['本地设备','远程路径','用户名','状态','类型','使用计数'];
    const head=document.createElement('tr');
    for(const name of columns){
      const cell=document.createElement('th');
      cell.textContent=name;
      head.append(cell);
    }
    this.head.replaceChildren(head);
    this.body.replaceChildren(...records.map(record=>this.row(record)));
    this.summary.textContent=loaded?`${all.length} 项`:'';
    this.empty.hidden=records.length!==0;
    if(!this.connected)this.empty.textContent='Client 未连接';
    else if(!loaded)this.empty.textContent='尚未读取';
    else if(!records.length)this.empty.textContent=all.length?'没有匹配的项目':'没有网络共享';
    this.newButton.disabled=!this.connected;
  }

  row(record){
    const row=document.createElement('tr'),published=record.kind===PUBLISHED;
    const values=published?
      [record.identity,record.name,shareType(record.flags),record.state,
       Number(record.value)===UNLIMITED?'无限制':record.value,record.description]:
      [record.detail||'',record.name,record.description,useState(record.state),useType(record.flags),record.value];
    row.record=record;
    for(const value of values){
      const cell=row.insertCell();
      cell.textContent=value??'';
      cell.title=cell.textContent;
    }
    return row;
  }

  openMenu(event,record){
    const actions=record.kind===PUBLISHED?
      [['编辑',()=>this.edit(record)],['权限',()=>this.permissions(record)],
       ['删除',()=>this.removePublished(record),true],['属性',()=>this.properties(record)]]:
      [['编辑/重新连接',()=>this.edit(record)],['断开',()=>this.disconnectConnection(record),true],
       ['属性',()=>this.properties(record)]];
    this.menu.replaceChildren(...actions.map(([title,action,danger])=>{
      const button=document.createElement('button');
      button.textContent=title;
      button.classList.toggle('danger',danger===true);
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

  edit(record=null){
    if(this.view==='published'){
      this.publishedRecord=record;
      const form=this.publishedDialog.querySelector('form');
      form.reset();
      form.querySelector('h2').textContent=record?'编辑共享':'新建文件夹共享';
      const name=form.querySelector('[data-field=name]'),path=form.querySelector('[data-field=path]');
      const maximum=form.querySelector('[data-field=maximum]');
      name.value=record?.identity||'';
      name.readOnly=!!record;
      path.value=record?.name||'';
      path.readOnly=!!record;
      form.querySelector('[data-action=browse]').hidden=!!record;
      form.querySelector('[data-field=remark]').value=record?.description||'';
      maximum.value=record?.value||String(UNLIMITED);
      form.querySelector('[data-role=maximum]').hidden=!record;
      form.querySelector('[data-role=published-note]').textContent=record?
        'Windows 不支持直接修改共享名称或本地路径；如需改变，请删除后重新创建。':
        '新共享默认不限制并发连接数，可创建后再修改。';
      this.publishedDialog.showModal();
      name.focus();
      return;
    }
    this.connectionRecord=record;
    const form=this.connectionDialog.querySelector('form');
    form.reset();
    form.querySelector('h2').textContent=record?'编辑并重新连接':'新建网络连接';
    form.querySelector('[data-field=remote]').value=record?.name||'';
    form.querySelector('[data-field=local]').value=record?.detail||'';
    form.querySelector('[data-field=username]').placeholder=record?.description?
      `当前连接：${record.description}`:'留空使用当前身份';
    this.connectionDialog.showModal();
    form.querySelector('[data-field=remote]').focus();
  }

  async browse(){
    const input=this.publishedDialog.querySelector('[data-field=path]');
    const path=await this.filePicker.open({mode:'folder',initialPath:input.value});
    if(path)input.value=path;
  }

  async savePublished(){
    const form=this.publishedDialog.querySelector('form'),record=this.publishedRecord;
    const identity=form.querySelector('[data-field=name]').value.trim();
    const path=form.querySelector('[data-field=path]').value.trim();
    const remark=form.querySelector('[data-field=remark]').value;
    try{
      await this.call('/api/network-shares/published/control',{
        action:record?23:1,
        identity,
        argument:record?form.querySelector('[data-field=maximum]').value:path,
        secret:remark
      });
      this.publishedDialog.close();
      this.notify(record?'共享已修改':'共享已创建');
      await this.load();
    }catch(error){
      this.notify(error);
    }
  }

  async removePublished(record){
    if(!confirm(`确定删除共享“${record.identity}”？现有客户端连接会受到影响。`))return;
    try{
      await this.call('/api/network-shares/published/control',{action:2,identity:record.identity});
      this.notify('共享已删除');
      await this.load();
    }catch(error){
      this.notify(error);
    }
  }

  async permissions(record){
    try{
      await this.aclEditor.open({
        title:`${record.identity} — 共享权限`,
        objectType:'share',
        load:async()=>{
          const records=await this.call('/api/network-shares/published/query',{identity:record.identity});
          if(records.length!==1)throw new Error('共享权限响应无效');
          return{sddl:records[0].detail};
        },
        save:sddl=>this.call('/api/network-shares/published/control',{
          action:26,
          identity:record.identity,
          argument:sddl
        })
      });
    }catch(error){
      this.notify(error);
    }
  }

  async saveConnection(){
    const form=this.connectionDialog.querySelector('form'),record=this.connectionRecord;
    const remote=form.querySelector('[data-field=remote]').value.trim();
    const local=form.querySelector('[data-field=local]').value.trim().toUpperCase();
    const username=form.querySelector('[data-field=username]').value.trim();
    const password=form.querySelector('[data-field=password]').value;
    if(record&&!confirm('修改连接需要先断开现有连接，然后重新连接。是否继续？'))return;
    try{
      if(record){
        await this.call('/api/network-shares/connections/control',{action:25,identity:record.identity});
      }
      await this.call('/api/network-shares/connections/control',{
        action:24,
        identity:remote,
        argument:local,
        secret:`${username}\0${password}`
      });
      this.connectionDialog.close();
      this.notify(record?'已重新连接':'连接已建立');
      await this.load();
    }catch(error){
      this.notify(error);
    }
  }

  async disconnectConnection(record){
    if(!confirm(`确定断开“${record.name}”？存在打开的文件时操作会被拒绝。`))return;
    try{
      await this.call('/api/network-shares/connections/control',{action:25,identity:record.identity});
      this.notify('连接已断开');
      await this.load();
    }catch(error){
      this.notify(error);
    }
  }

  properties(record){
    const published=record.kind===PUBLISHED;
    const fields=published?
      [['共享名称',record.identity],['本地路径',record.name],['类型',shareType(record.flags)],
       ['当前连接',String(record.state)],
       ['最大连接',Number(record.value)===UNLIMITED?'无限制':record.value],
       ['备注',record.description||'—']]:
      [['本地设备',record.detail||'—'],['远程路径',record.name],
       ['用户名',record.description||'当前身份'],['状态',useState(record.state)],
       ['类型',useType(record.flags)],['使用计数',record.value]];
    this.propertiesDialog.querySelector('dl').replaceChildren(...fields.flatMap(([name,value])=>{
      const dt=document.createElement('dt'),dd=document.createElement('dd');
      dt.textContent=name;
      dd.textContent=value;
      return[dt,dd];
    }));
    this.propertiesDialog.showModal();
  }
}
