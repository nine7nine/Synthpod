# Get list of plugins (ui -> dsp)
	a patch:Get ;
	patch:subject spod:stereo ;
	patch:property spod:pluginList .

# Set list of plugins (dsp -> ui)
	a patch:Set ;
	patch:subject spod:stereo ;
	patch:property spod:pluginList ;
	patch:value [
		a atom:Tuple ;
		rdf:value (
			<URI>
			<URI>
			<URI>
			<URI>
			<URI>
		)
	] .


# Get plugin properties (ui -> dsp)
	a patch:Get ;
	patch:subject <URI> .

# Set plugin properties (dsp -> ui)
	a patch:Put ;
	patch:subject <URI> ;
	patch:body [
		lv2:name "NAME" ;
		rdfs:comment "COMMENT" ;
	] .


# Get list of modules (ui -> dsp)
	a patch:Get ;
	patch:subject spod:stereo ;
	patch:property spod:moduleList .

# Set list of modules (dsp -> ui)
	a patch:Set ;
	patch:subject spod:stereo ;
	patch:property spod:moduleList ;
	patch:value [
		a atom:Tuple ;
		rdf:value (
			spod:module#1
			spod:module#3
			spod:module#2
		)
	] .


# Add module (ui -> dsp)
	a patch:Insert ;
	patch:subject spod:stereo ;
	patch:body [
		lv2:Plugin <URI> ;
	] .


# Get all module properties (ui -> dsp)
	a patch:Get ;
	patch:subject spod:module#1 .

# Set all module properties (dsp ->ui)
	a patch:Put ;
	patch:subject spod:module#1
	patch:body [
		lv2:Plugin <URI> ;
		spod:enabled true ;
		spod:visible true ;
		lv2:Port [
			a atom:Tuple ;
			rdf:value (
				spod:module1#symbol
				spod:module1#symbol
			)
		] ;
	] .


# Get individual module properties (ui -> dsp)
	a patch:Get ;
	patch:subject spod:module#1 ;
	patch:property rdfs:label .

# Set individual module properties (dsp -> ui)
	a patch:Set ;
	patch:subject spod:module#1 ;
	patch:property rdfs:label ;
	patch:value "LABEL" .


# Get port properties (ui -> dsp)
	a patch:Get ;
	patch:subject spod:module#1#symbol .

# Set port properties (dsp -> ui)
	a patch:Put ;
	patch:subject spod:module#1#symbol ;
	patch:body [
		rdf.value 0.5 ;
		spod:enabled true ;
		spod:visible true ;
		spod:sources [
			a atom:Tuple ;
			rdf:value (
				spod:module#3#symbol ;
				spod:module#3#symbol ;
			)
		]
	] .

# Set port value (ui <-> ui)
	a patch:Set ;
	patch:subject spod:module#1#symbol ;
	patch:property rdf.value ;
	patch:value 0.2 .
