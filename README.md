# http-loop
Ferramenta para estressar servidores HTTP baseado em lista de URLs

Em breve opcao de analisar o html e baixar todos os links de primeiro nivel.

<pre>

gcc http-loop.c -o /usr/bin/http-loop

AJUDA:
   http-loop -h

Exemplo:
   http-loop -d www.microsoft.com

Recursos a desenvolver:
   -x       : baixar referencias de links na pagina principal (css, js, imagens)
              para simular melhor um usuario navegando

   -r       : randomizar lista de sites, assim links quebrados terao menos impacto
              na simulacao

   -i       : modo informativo (verbose, debug level 1), mostrar urls sendo baixadas
              e velocidade consumida

</pre>
