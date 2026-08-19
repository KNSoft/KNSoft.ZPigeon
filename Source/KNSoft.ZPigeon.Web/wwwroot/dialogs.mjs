const negativeValue=/^(cancel|close|no)$/i;
const negativeAction=/(?:^|-)(?:cancel|close)(?:-|$)/i;
const positiveValue=/^(?:ok|yes|save|create|run|rename|paste|format|default)$/i;
const positiveAction=/(?:^|-)(?:ok|save|create|run|rename|paste|format|apply|start)(?:-|$)/i;
const positiveText=new Set(['确定','是','保存','创建','执行','打开','运行','重命名','写入','格式化','仍然粘贴','创建并下载']);

const buttons=dialog=>[...dialog.querySelectorAll('.dialog-actions>button')];
const negative=button=>negativeValue.test(button.value)||negativeAction.test(button.dataset.action||'')||
  ['取消','关闭','否'].includes(button.textContent.trim());
const dangerous=button=>button.classList.contains('danger')||/(?:^|-)(?:delete|remove|clear|format|uninstall)(?:-|$)/i.test(button.dataset.action||'');

function defaultButton(dialog){
  const values=buttons(dialog),explicit=values.find(button=>button.dataset.default!==undefined);
  if(explicit)return explicit;
  const submit=values.find(button=>button.type==='submit'&&!negative(button)&&!dangerous(button));
  if(submit)return submit;
  const positive=values.find(button=>!dangerous(button)&&(positiveValue.test(button.value)||
    positiveAction.test(button.dataset.action||'')||positiveText.has(button.textContent.trim())));
  if(positive)return positive;
  if(values.some(dangerous))return values.find(negative)??null;
  return values.length===1?values[0]:null;
}

function prepare(dialog){
  for(const actions of dialog.querySelectorAll('.dialog-actions')){
    const values=[...actions.children].filter(element=>element instanceof HTMLButtonElement);
    for(const button of values)button.classList.toggle('dialog-negative',negative(button));
    const ordered=values.filter(button=>!negative(button)).concat(values.filter(negative));
    if(ordered.some((button,index)=>button!==values[index]))for(const button of ordered)actions.append(button);
  }
  const button=defaultButton(dialog);
  for(const value of buttons(dialog))value.toggleAttribute('data-dialog-default',value===button);
}

export function installDialogBehavior(){
  const observer=new MutationObserver(records=>{
    for(const record of records)for(const node of record.addedNodes)if(node instanceof Element){
      if(node.matches('dialog'))prepare(node);
      for(const dialog of node.querySelectorAll?.('dialog')??[])prepare(dialog);
    }
  });
  observer.observe(document.body,{childList:true,subtree:true});
  for(const dialog of document.querySelectorAll('dialog'))prepare(dialog);
  document.addEventListener('keydown',event=>{
    if(event.key!=='Enter'||event.altKey||event.ctrlKey||event.metaKey||event.shiftKey||
       event.target instanceof HTMLTextAreaElement||event.target instanceof HTMLButtonElement)return;
    const dialog=event.target.closest('dialog');
    if(!dialog?.open)return;
    prepare(dialog);
    const button=defaultButton(dialog);
    if(button&&!button.disabled){event.preventDefault();button.click()}
  });
}
