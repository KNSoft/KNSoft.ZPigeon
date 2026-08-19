const collator=new Intl.Collator(undefined,{numeric:true,sensitivity:'base'}),selector='.manager-table table,.picker-list table,.registry-list table,.event-list table';

function compare(left,right){
  const a=left.dataset.sortValue??left.textContent.trim(),b=right.dataset.sortValue??right.textContent.trim(),an=Number(a.replaceAll(',','')),bn=Number(b.replaceAll(',',''));
  return Number.isFinite(an)&&Number.isFinite(bn)?an-bn:collator.compare(a,b);
}

function sort(table,index,direction){
  const body=table.tBodies[0];if(!body)return;
  const rows=[...body.rows].map((row,order)=>({row,order}));
  rows.sort((left,right)=>direction*compare(left.row.cells[index]??left.row,right.row.cells[index]??right.row)||left.order-right.order);
  if(rows.some((item,index)=>item.row!==body.rows[index]))body.append(...rows.map(item=>item.row));
}

function prepare(table){
  if(table.dataset.tableReady)return;table.dataset.tableReady='true';
  let column=-1,direction=1,resizing=false;
  const configure=()=>{
    const headers=[...table.tHead?.rows[0]?.cells??[]];
    headers.forEach((header,index)=>{
      if(header.dataset.tableHeader)return;header.dataset.tableHeader='true';header.tabIndex=0;header.classList.add('sortable-column');
      const resize=document.createElement('span');resize.className='column-resizer';resize.setAttribute('aria-hidden','true');header.append(resize);
      const apply=()=>{if(resizing)return;if(column===index)direction=-direction;else{column=index;direction=1}headers.forEach(item=>item.removeAttribute('aria-sort'));header.setAttribute('aria-sort',direction>0?'ascending':'descending');sort(table,column,direction)};
      header.onclick=apply;header.onkeydown=event=>{if(event.key==='Enter'||event.key===' '){event.preventDefault();apply()}};
      resize.onpointerdown=event=>{event.preventDefault();event.stopPropagation();resizing=true;resize.setPointerCapture(event.pointerId);const widths=headers.map(item=>item.getBoundingClientRect().width),width=widths[index],total=widths.reduce((sum,value)=>sum+value,0),start=event.clientX;headers.forEach((item,index)=>item.style.width=`${widths[index]}px`);resize.onpointermove=move=>{const next=Math.max(48,width+move.clientX-start);header.style.width=`${next}px`;table.style.width=`${Math.max(table.parentElement.clientWidth,total-width+next)}px`};resize.onpointerup=()=>{resizing=false;resize.onpointermove=resize.onpointerup=null}};
    });
  };
  configure();
  new MutationObserver(()=>{configure();if(column>=0)sort(table,column,direction)}).observe(table,{childList:true,subtree:true});
}

export function enableManagedTables(root=document){
  root.querySelectorAll(selector).forEach(prepare);
  new MutationObserver(records=>records.forEach(record=>record.addedNodes.forEach(node=>{if(node.nodeType!==Node.ELEMENT_NODE)return;if(node.matches?.(selector))prepare(node);node.querySelectorAll?.(selector).forEach(prepare)}))).observe(root,{childList:true,subtree:true});
}
