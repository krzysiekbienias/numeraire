

console.log(window.location)
const tradeUrl=window.location.href

const formModal=document.getElementById("form-modal-trades")

const openModalBtn=document.getElementById("open-modal-btn")
openModalBtn.addEventListener('click', ()=>{
    formModal.classList.remove('hidden')

})

const cancelModalBtn=document.getElementById("cancel-modal-btn")
cancelModalBtn.addEventListener('click' ,()=>{
    formModal.classList.add('hidden')
})



$.ajax({
    type:"GET",
    url:"trade-book",
    success: function(response){
        console.log(response)

        const data=response.trades
        // add headers
        const headers=
        `<thead>`+      
                +`<tr>`
                    +`<th scope="col">Trade ID</th>`
                    +`<th scope="col">Underlier Ticker</th>`
                    +`<th scope="col">Product Type</th>`
                    +`<th scope="col">Payoff</th>`
                    +`<th scope="col">Trade Date</th>`
                    +`<th scope="col">Trade Maturity</th>`
                    +`<th scope="col">Strike</th>`
                    +`<th scope="col">Dividend</th>`
                    +`<th scope="col">user_id</th>`

                +`<tr>`+
        `<thead>`
        $('#trade-book-table').append(headers)  
        // add table body     
        data.forEach(el => {
        new_row=`<tr>`
        +`<td>`+`<a href="${tradeUrl}/${el.pk}">${el.pk}</a>`+`</td>`
        +`<td>`+el.underlier_ticker+`</td>`
        +`<td>`+el.product_type+`</td>`
        +`<td>`+el.payoff+`</td>`
        +`<td>`+el.trade_date+`</td>`
        +`<td>`+el.trade_maturity+`</td>`
        +`<td>`+el.strike+`</td>`
        +`<td>`+el.dividend+`</td>`
        +`<td>`+el.user_id+`</td>`

        +`<tr>`
        $('#trade-book-table').append(new_row) 
            
        });
        
        console.log(data)

    },
    error:function(error){
        console.log(error)
    }
})





