# juxta_vdd

SAADC helper for Tag VDD (CR2032 / direct supply). Requires overlay matching
Nordic’s nrf54l15tag fuel-gauge channel (GAIN_1_4 + 0.9 V ref). With `ADC_GAIN_1`
the converted reading is ~VDD/4 (~900 mV on a 3 V cell).

```dts
/ {
	zephyr,user {
		io-channels = <&adc 0>;
	};
};

&adc {
	#address-cells = <1>;
	#size-cells = <0>;
	status = "okay";

	channel@0 {
		reg = <0>;
		zephyr,gain = "ADC_GAIN_1_4";
		zephyr,reference = "ADC_REF_INTERNAL";
		zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>;
		zephyr,resolution = <14>;
		zephyr,oversampling = <8>;
		zephyr,vref-mv = <900>;
		zephyr,input-positive = <NRF_SAADC_VDD>;
	};
};
```

API: `juxta_vdd_init()`, `juxta_vdd_read_mv()`, `juxta_vdd_mv_to_percent_cr2032()`.
